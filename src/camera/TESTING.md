# Running Camera Tests

All camera tests should be run on all current targets before submitting code!

## To test the camera stack:

1. `fx set [product].[board] --with //bundles:tests`
2. You may have to re-flash zedboot if you flashed with a different fx set command
3. `fx serve`
4. Restart the target device in the netboot cable configuration
5. Run `fx run-test camera_tests`

Note: most on-device tests require the camera to be physically enabled (i.e. the device must not be
muted). If a test determines that the device may be muted, a warning will be displayed and the test
will be skipped.


## To test camera functionality manually:

1. Follow steps 1 throuh 4 for on-device test setup
2. Run `fx shell tiles_ctl start`
3. Run `fx shell tiles_ctl add fuchsia-pkg://fuchsia.com/camera_display#meta/camera_display.cmx`


## To run single tests:
Single components can be tested by following the instructions above, and
adding a ```-t test_name``` to the end.  For example:

    fx run-test camera_tests -t gdc-task


