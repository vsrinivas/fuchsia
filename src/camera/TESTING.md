# Running Camera Tests

## To test the on-device functionality:

1. `fx set [product].[board] --with //bundles:tests`
2. You may have to re-flash zedboot if you flashed with a different fx set command
3. `fx serve`
4. Restart the target device in the netboot cable configuration
5. Run `fx run-test camera_full_on_device_test`


## To test camera functionality manually:

1. Follow steps 1 throuh 4 for on-device test setup
2. Run `fx shell tiles_ctl start`
3. Run `fx shell tiles_ctl add fuchsia-pkg://fuchsia.com/camera_demo#meta/camera_demo.cmx`


## To test camera libraries off-device:
You can follow the directions above to test on the device, and run:
`fx run-test camera_full_test`

Or:

1. `fx set workstation.x64 --with //bundles:tests`
2. `fx build`
3. `fx serve`
4. In another terminal: `fx run -k -N`
5. In another terminal: `fx run-test camera_full_test`

## To run single tests:
Single components can be tested by following the instructions above, and
adding a ```-t test_name``` to the end.  For example:

    fx run-test camera_full_test -t gdc-task
