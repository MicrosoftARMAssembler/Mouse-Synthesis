#pragma once
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <mutex>

namespace mouse {
	struct mouse_input_data_t {
		std::uint16_t unit_id;
		std::uint16_t flags;
		std::uint16_t button_flags;
		std::uint16_t button_data;
		std::uint32_t raw_buttons;
		std::int32_t  last_x;
		std::int32_t  last_y;
		std::uint32_t extra_information;
	};

	static_assert( sizeof( mouse_input_data_t ) == 0x18 );

	enum mouse_packet_flags : std::uint16_t {
		packet_relative = 0x0000,
		packet_absolute = 0x0001,
		virtual_desktop = 0x0002,
		move_no_coalesce = 0x0008,
	};

	enum mouse_button_flags : std::uint16_t {
		left_down = 0x0001,
		left_up = 0x0002,
		right_down = 0x0004,
		right_up = 0x0008,
		middle_down = 0x0010,
		middle_up = 0x0020,
		wheel = 0x0400,
		h_wheel = 0x0800,
	};

	class c_mouse {
	public:
		bool setup( ) {
			m_pdb = std::make_unique<pdb::c_pdb>(
				obf( "win32kbase.sys" ) );

			if ( !m_pdb || !m_pdb->load( ) ) {
				logging::print(
					obf( "failed to load win32kbase.sys pdb" ) );
				return false;
			}

			m_synthesize_mouse_input =
				m_pdb->get_symbol_address(
					obf( "SynthesizeMouseInput" ) );

			if ( !m_synthesize_mouse_input ) {
				logging::print(
					obf( "failed to find SynthesizeMouseInput" ) );
				return false;
			}

			m_unit_id = 1; // Placeholder recommendation: synthesis input using a real hardware id to improve packet detections - hasn't been a problem yet 

			logging::print(
				obf( "using mouse unit: 0x%x" ),
				m_unit_id );

     		// we're executing this function from usermode using system call hooking in NTOSKRNL.exe.
     		// Since our system call hook is inside of NTOSKRNL.exe, we need to allocate a shellcode stub that's inside of ntoskrnl to call our function inside of WIN32KBASE.sys.
     		// If you're reimplementing this into your driver you don't need to generate your own shellcode stub.
      
			m_stub_page = mapper::allocate_large_page(
				obf( "ntoskrnl.exe" ),
				0x1000 );

			if ( !m_stub_page ) {
				logging::print(
					obf( "failed to allocate stub page" ) );
				return false;
			}

			logging::print(
				obf( "allocated stub: 0x%llx" ),
				m_stub_page );

			if ( !generate_call_stub( ) ) {
				logging::print(
					obf( "failed to generate call stub" ) );
				return false;
			}

			return true;
		}

		bool move(
			double desktop_x,
			double desktop_y,
			bool use_virtual_desktop = true )
		{
			return move_absolute_pixels(
				desktop_x,
				desktop_y,
				use_virtual_desktop );
		}

		bool move_client(
			HWND window,
			double client_x,
			double client_y,
			bool use_virtual_desktop = true )
		{
			if ( !window || !IsWindow( window ) ||
				!std::isfinite( client_x ) ||
				!std::isfinite( client_y ) )
			{
				return false;
			}

			POINT point {
				static_cast< LONG >( std::llround( client_x ) ),
				static_cast< LONG >( std::llround( client_y ) )
			};

			if ( !ClientToScreen( window, &point ) )
				return false;

			return move_absolute_pixels(
				static_cast< double >( point.x ),
				static_cast< double >( point.y ),
				use_virtual_desktop );
		}

		bool click_lmb( ) {
			if ( !call_synthesize(
				0,
				0,
				packet_absolute,
				left_down,
				0 ) )
			{
				return false;
			}

			return call_synthesize(
				0,
				0,
				packet_absolute,
				left_up,
				0 );
		}

		bool click_lmb_at(
			double desktop_x,
			double desktop_y,
			bool use_virtual_desktop = true )
		{
			std::int32_t normalized_x = 0;
			std::int32_t normalized_y = 0;

			if ( !normalize_absolute_point(
				desktop_x,
				desktop_y,
				use_virtual_desktop,
				normalized_x,
				normalized_y ) )
			{
				return false;
			}

			const auto packet_flags =
				static_cast< std::uint16_t >(
					packet_absolute |
					move_no_coalesce |
					( use_virtual_desktop
						? virtual_desktop
						: 0 ) );

			if ( !call_synthesize(
				normalized_x,
				normalized_y,
				packet_flags,
				left_down,
				0 ) )
			{
				return false;
			}

			return call_synthesize(
				normalized_x,
				normalized_y,
				packet_flags,
				left_up,
				0 );
		}

	private:
		std::unique_ptr<pdb::c_pdb> m_pdb { };
		std::uint64_t m_synthesize_mouse_input { };
		std::uint64_t m_stub_page { };
		std::uint64_t m_mouse_input_data { };
		std::uint16_t m_unit_id { 0 };
		std::mutex m_input_mutex;

		bool move_absolute_pixels(
			double desktop_x,
			double desktop_y,
			bool use_virtual_desktop )
		{
			std::int32_t normalized_x = 0;
			std::int32_t normalized_y = 0;

			if ( !normalize_absolute_point(
				desktop_x,
				desktop_y,
				use_virtual_desktop,
				normalized_x,
				normalized_y ) )
			{
				return false;
			}

			const auto packet_flags =
				static_cast< std::uint16_t >(
					packet_absolute |
					move_no_coalesce |
					( use_virtual_desktop
						? virtual_desktop
						: 0 ) );

			return call_synthesize(
				normalized_x,
				normalized_y,
				packet_flags,
				0,
				0 );
		}

		static bool normalize_absolute_point(
			double desktop_x,
			double desktop_y,
			bool use_virtual_desktop,
			std::int32_t& normalized_x,
			std::int32_t& normalized_y )
		{
			if ( !std::isfinite( desktop_x ) ||
				!std::isfinite( desktop_y ) )
			{
				return false;
			}

			const int desktop_left = use_virtual_desktop
				? GetSystemMetrics( SM_XVIRTUALSCREEN )
				: 0;

			const int desktop_top = use_virtual_desktop
				? GetSystemMetrics( SM_YVIRTUALSCREEN )
				: 0;

			const int desktop_width = use_virtual_desktop
				? GetSystemMetrics( SM_CXVIRTUALSCREEN )
				: GetSystemMetrics( SM_CXSCREEN );

			const int desktop_height = use_virtual_desktop
				? GetSystemMetrics( SM_CYVIRTUALSCREEN )
				: GetSystemMetrics( SM_CYSCREEN );

			if ( desktop_width <= 1 || desktop_height <= 1 )
				return false;

			const double desktop_right =
				static_cast< double >(
					desktop_left + desktop_width - 1 );

			const double desktop_bottom =
				static_cast< double >(
					desktop_top + desktop_height - 1 );

			if ( desktop_x < static_cast< double >( desktop_left ) ||
				desktop_x > desktop_right ||
				desktop_y < static_cast< double >( desktop_top ) ||
				desktop_y > desktop_bottom )
			{
				logging::print(
					obf(
						"rejected absolute point: "
						"x=%.2f y=%.2f bounds=[%d,%d -> %.0f,%.0f]" ),
					desktop_x,
					desktop_y,
					desktop_left,
					desktop_top,
					desktop_right,
					desktop_bottom );

				return false;
			}

			const double scaled_x =
				( desktop_x - static_cast< double >( desktop_left ) ) *
				65535.0 /
				static_cast< double >( desktop_width - 1 );

			const double scaled_y =
				( desktop_y - static_cast< double >( desktop_top ) ) *
				65535.0 /
				static_cast< double >( desktop_height - 1 );

			normalized_x = static_cast< std::int32_t >(
				std::llround(
					std::clamp( scaled_x, 0.0, 65535.0 ) ) );

			normalized_y = static_cast< std::int32_t >(
				std::llround(
					std::clamp( scaled_y, 0.0, 65535.0 ) ) );

			return true;
		}

		bool generate_call_stub( ) {
			std::uint8_t shellcode[ 256 ] { };
			std::size_t position = 0;

			const auto emit = [ & ](
				std::initializer_list<std::uint8_t> bytes )
				{
					for ( const auto byte : bytes )
						shellcode[ position++ ] = byte;
				};

			const auto emit64 = [ & ]( std::uint64_t value ) {
				std::memcpy(
					shellcode + position,
					&value,
					sizeof( value ) );

				position += sizeof( value );
				};

			emit( { 0x4C, 0x8B, 0x54, 0x24, 0x28 } );
			emit( { 0x4C, 0x8B, 0x5C, 0x24, 0x30 } );
			emit( { 0x48, 0x83, 0xEC, 0x38 } );
			emit( { 0x4C, 0x89, 0x54, 0x24, 0x20 } );
			emit( { 0x4C, 0x89, 0x5C, 0x24, 0x28 } );
			emit( { 0x48, 0xB8 } );
			emit64( m_synthesize_mouse_input );
			emit( { 0xFF, 0xD0 } );
			emit( { 0x48, 0x83, 0xC4, 0x38 } );
			emit( { 0xC3 } );

			if ( !memory::write_virtual(
				m_stub_page,
				shellcode,
				position ) )
			{
				return false;
			}

			logging::print(
				obf( "generated stub: 0x%llx" ),
				m_synthesize_mouse_input );

			return true;
		}

		bool call_synthesize(
			std::int32_t x,
			std::int32_t y,
			std::uint16_t packet_flags,
			std::uint16_t button_flags,
			std::uint16_t button_data )
		{
			std::lock_guard lock( m_input_mutex );

			mouse_input_data_t input { };
			input.unit_id = m_unit_id;
			input.flags = packet_flags;
			input.button_flags = button_flags;
			input.button_data = button_data;
			input.raw_buttons = 0;
			input.last_x = x;
			input.last_y = y;
			input.extra_information = 0;

      		// We allocate a kernel mode buffer for the structure since we're in usermode.
      		// For example similar to what we do in generate_call_stub, if you're moving this into a kernel mode it would be recommeneded to change this.
			if ( !m_mouse_input_data ) {
				m_mouse_input_data =
					kernel::allocate_pages(
						sizeof( mouse_input_data_t ) )
					.value_or( 0 );

				if ( !m_mouse_input_data ) {
					logging::print(
						obf( "failed to allocate input buffer" ) );
					return false;
				}
			}

			if ( !memory::write_virtual(
				m_mouse_input_data,
				&input,
				sizeof( input ) ) )
			{
				logging::print(
					obf( "failed to setup input buffer" ) );
				return false;
			}

			auto event_time =
				static_cast< std::uint64_t >(
					GetTickCount64( ) );

			if ( !event_time )
				event_time = 1;

			LARGE_INTEGER qpc { };
			QueryPerformanceCounter( &qpc );

			const auto qpc_time =
				static_cast< std::uint64_t >(
					qpc.QuadPart );

			const auto proc_flags =
				( button_flags != 0 && x == 0 && y == 0 )
				? 0x118u
				: 0x110u;

			g_syscall->call_kernel<void>(
				m_stub_page,
				0,
				m_mouse_input_data,
				event_time,
				qpc_time,
				proc_flags,
				0 );

			return true;
		}
	};
}
