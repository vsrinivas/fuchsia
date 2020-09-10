# Screen is not black test

`screen_is_not_black_test` reboots the device under test and verifies that its
screen is not black within 100 seconds after reboot.

This test is enabled for the terminal and workstation products.

There are two variants of this test:

*   `screen_is_not_black_test` - It launches `basemgr` and can only be used on
    products that use the Modular framework.
*   `screen_is_not_black_no_basemgr_test` - The `no_basemgr` variant should be
    used on products that use Session Framework, like workstation.
