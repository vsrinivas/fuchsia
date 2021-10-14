# Screen is not black test

`screen_is_not_black_test` reboots the device under test and verifies that its
screen is not black within 100 seconds after reboot.

This test is enabled for the terminal and workstation products.

The test can optionally start up `basemgr` or `tiles` before checking the screen. Use this
mode for configurations that do not start up any graphical environment and are capable
of running either `basemgr` or `tiles`.

Test targets:

*   `screen_is_not_black` - Reboots the device and examines the screen without starting any
additional programs. Use this in configurations that start up a graphical environment
by default.

*   `basemgr_test` - Starts `basemgr` and verifies the screen is not black.

*   `tiles_test` - Starts `tiles` and verifies the screen is not black
    products that use the `root_presenter`.

Target aliases provided for compatibility:

*   `test` - Alias for `basemgr_test`