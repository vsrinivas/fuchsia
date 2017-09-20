Vulkan Development
==================

1) Runtime dependencies

    The magma driver and libraries should already be built into a complete fuchsia image, however you should have your project depend on the 'magma' package to be sure that the necessary files are included in the system image of whatever build includes your project.

2) Buildtime dependencies

    In order for your project to access the vulkan headers, and to link against the vulkan loader libvulkan.so, add the following GN dependency:

    //magma:vulkan

3) Rendering onscreen

    There are two options for displaying your rendered output:

    a) The system compositor

    See mozart documentation for details.

    b) Directly to the display

    This method is not compatible with a system that has a system compositor.

    You can use a custom version of the WSI swapchain:

    https://www.khronos.org/registry/vulkan/specs/1.0-extensions/html/vkspec.html#_wsi_swapchain

    For details on the magma custumization, refer to the vkcube example here:

    third_party/vkcube/cube.cc

4) Interaction with the graphics console

    The magma display driver supports toggling ownership between the main display owner, and the graphics console.

    Currently, on system startup the gfxconsole owns the display.

    When a vulkan application starts, it will take over the display.

    To toggle display ownership between the vulkan app and the gfxconsole, press alt-esc.

5) Reporting issues

    Keep an eye on the system log for various types of graphics driver specific issues, and file tickets on the Magma project.
    The driver should kill the connection corresponding to the context that was executing when these issues occurred; but otherwise should handle this failure gracefully.  
    If nothing works afterward, please file that as an issue as well.

    a) Gpu fault

    Looks something like the following.  This can happen due to user error or driver bug.  Please make sure your app has no validation layer issues.
    If you believe your app is innocent, please file a Magma ticket and include at least this portion of the log, plus ideally a recipe to repro:

        > [WARNING] GPU fault detected
        > ---- device dump begin ----
        > RELEASE build
        > Device id: 0x1916
        > RENDER_COMMAND_STREAMER
        > sequence_number 0x1003
        > active head pointer: 0x1f328
        > ENGINE FAULT DETECTED
        > engine 0x0 src 0x3 type 0x0 gpu_address 0x1000000
        > mapping cache footprint 11.9 MB cap 190.0 MB
        > ---- device dump end ----
        > [WARNING] resetting render engine

    b) Gpu hang

    If a command buffer fails to complete within a certain amount of time, the gpu driver should detect the condition and treat it as if a fault occured.
    Again, may be an application error or driver bug. If you believe your app is innocent, please file a Magma ticket and include at least this portion of the log, plus ideally a recipe to repro:

        > [WARNING] Suspected GPU hang: last submitted sequence number 0x1007 master_interrupt_control 0x80000000
        > ---- device dump begin ----
        > DEBUG build
        > Device id: 0x1916
        > RENDER_COMMAND_STREAMER
        > sequence_number 0x1006
        > active head pointer: 0x20
        > No engine faults detected.
        > mapping cache footprint 0.0 MB cap 0.0 MB
        > ---- device dump end ----
        > [WARNING] resetting render engine

6) Demo

    The magma build includes a spinning cube demo 'vkcube', which you can copy over to your fuchsia system and execute via netruncmd.
