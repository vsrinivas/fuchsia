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

4) Demo

    The magma build includes a spinning cube demo 'vkcube', which you can copy over to your fuchsia system and execute via netruncmd.
