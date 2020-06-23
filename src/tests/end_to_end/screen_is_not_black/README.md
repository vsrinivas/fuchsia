# Screen is not black

Verifies that the screen is not black after booting.

This test is enabled for the terminal and workstation products.

There are two variants of this test, `screen_is_not_black_test` and
`screen_is_not_black_no_basemgr_test`. The former launches basemgr and can
only be used on products that use the Modular framework. The `no_basemgr`
variant should be used on products that use Session Framework, like workstation.
