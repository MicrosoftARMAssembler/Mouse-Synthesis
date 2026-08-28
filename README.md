# Mouse-Synthesis

Mouse-Synthesise synthesize HIDs directly from hardware **bypassing higher-level detections**. <br /> 
To synthesize HIDs we call ```SynthesizeMouseInput``` injecting input at the **same level HID hardware delivers** it. <br /> 
It's **better** than public methods like mouclass HID injection because it **enters the stack below every tracked layer**, meaning **there is nothing to hook or monitor**.  <br /> 

# What makes Mouse-Synthesis work?
Mouse-Synthesis resolves ```SynthesizeMouseInput``` from **win32kbase.sys** and calls it from kernel after **constructing a** ```MOUSE_INPUT_DATA``` **packet**. <br />
Windows compositor also calls ```SynthesizeMouseInput``` internally for **touch, RDP, and accessibility input**. <br />
We craft the packet to **match real physical HIDS** by setting ```unit_id``` to 1 so it **matches MOUHID.sys default assignment**. <br />

