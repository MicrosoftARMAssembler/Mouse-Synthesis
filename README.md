# Mouse-Synthesis

Mouse-Synthesise synthesize HIDs directly from hardware **bypassing higher-level detections**. <br /> 
To synthesize HIDs we call ```SynthesizeMouseInput``` injecting input at the **same level HID hardware delivers** it. <br /> 
HID synthetization is better than HID injection because it **enters the stack below every tracked layer** so **there's nothing to hook or monitor**.  <br /> 
If you are **looking for the missing dependencies** like PDB, kernel function call, e.g.. frameworks <a href="https://github.com/MicrosoftARMAssembler/Kunai-Driverless/tree/main/kunai-driverless">click here</a> for them.  <br /> 

# How does the synthesis work?
Mouse-Synthesis resolves ```SynthesizeMouseInput``` from **win32kbase.sys** and calls it from kernel after **constructing a** ```MOUSE_INPUT_DATA``` **packet**. <br />
Windows compositor also calls ```SynthesizeMouseInput``` internally for **touch, RDP, and accessibility input**. <br />
We craft the packet to **match real physical HIDs** by setting ```unit_id``` to 1 so it matches mouhid.sys **default assignment**. <br />

<img width="779" height="584" alt="image" src="https://github.com/user-attachments/assets/67bb9eaa-280b-4b55-8ebe-66b6e37a1ed6" />

# User-Mode Implementation
Mouse-Synthesis **runs entirely from user-mode** using our syscall hook inside **ntoskrnl.exe** to **call kernel functions**. <br />
Since we're **usermode** we need to **allocate live-kernel memory** instead of **stack allocated buffers** in a kernel mode driver. <br />
And because of the syscall hook we need to allocate a shellcode call stub to **read the arguments and then forward the call** since ```SynthesizeMouseInput``` is inside **win32kbase.sys**. <br />
If you are reimplementing this inside a kernel driver, **neither allocation for call stub or buffers are necessary**. <br />

```cpp
if ( !m_mouse_input_data ) {
    m_mouse_input_data =
        kernel::allocate_pages( sizeof( mouse_input_data_t ) )
        .value_or( 0 );
}

m_stub_page = mapper::allocate_large_page(
    obf( "ntoskrnl.exe" ),
    0x1000 );
```

# What can the synthesis do?
Mouse-Synthesis only uses **absolute movement** ( ```packet_absolute``` ) normalizing all coordinates. <br />
We haven't implemented **delta movement** ( ```packet_relative``` ) because games like **Fortnite** process raw input and will **reject relative packets**. <br />
The interface supports **mouse button clicks** and **mouse movement** using absolute or relative movements. <br />

# Follow my Github and check out my other projects!
