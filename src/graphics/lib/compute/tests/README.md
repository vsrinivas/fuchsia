This directory contains very simple tests written to check that the data types and
functions in common/vk/vk_app_state.h work properly.

vk_app_state_test:
  Simple creation / print / destruction of a vk_app_state_t instance,
  without enabling swapchain support.

vk_swapchain_test
  Simple creation / print / destruction of a vk_app_state_t instance,
  *with* swapchain support enabled, then of a vk_swapchain_t instance
  from it. However, nothing is displayed and the program exist
  immediately.

vk_triangle_test:
  Simple vulkan application that renders a gradient-shaded triangle on
  a window (on the host), or the framebuffer (on a Fuchsia device).
  Implements full presentation support with graphics pipeline operations
  only.

  NOTE: This is a very basic port of the Vulkan Tutorial's "Hello Triangle"
  example. The image is static even though it is re-rendered on every frame.
  A small tick (!) is printed to stdout every 120 frames (i.e. every 2s)
  to verify that it actually runs.

vk_transfer_test:
  A modified version of vk_triangle_test that will also transfer a
  host-visible buffer to the image. The buffer content changes on every frame,
  as well as its position. Used to verify that the presentation support
  correctly handles image layout transitions.


To build the tests:

    fx build src/graphics/lib/compute:all_tests
    # Rebuilds everything for host and device.

    fx build vk_triangle_test
    # Only rebuilds a specific device test program. Note that you cannot
    # install it directly (see below on how to run it).

    fx build host_x64/vk_triangle_test
    # Only rebuild a specific host test program.


To run the tests:

    out/default/host_x64/vk_triangle_test
    # Run the test on the host directly.

    (cd out/default && gdb --args host_x64/exe.unstripped/vk_triangle_test)
    # Run the test on the host inside a debugger. Changing the directory
    # allows the debugger to find the right sources directly.

    fx build updates && fx shell run vk_triangle_test
    # Install then run the test on the device. Building the "updates" target
    # is necessary to rebuilt the package repository that the device will
    # connect to in order to download the test binary. Without this, just
    # rebuilding the device test on the host has no effect on "fx shell run"
    # command.

    fx shell run vk_triangle_test
    # Re-run the test if already installed on the device.
