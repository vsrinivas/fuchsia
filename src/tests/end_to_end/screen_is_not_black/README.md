# Screen is not black test

`screen_is_not_black_test` reboots the device under test and verifies that its
screen is not black within 100 seconds after reboot.

This test is enabled for the `terminal` and `workstation` products.

The test can optionally start up [tiles] before checking the screen. Use this
mode for configurations that do not start up any graphical environment on boot
but are capable of running `tiles`, like the `terminal` product.

Test targets:

*   `screen_is_not_black` - Reboots the device and examines the screen without
    starting any additional programs. Use this in configurations that
    start up a graphical environment by default.

*   `tiles_test` - Starts [tiles] and verifies the screen is not black on
    products that use it. This will cause `root_presenter` to start as well.

[tiles]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/tools/tiles/
