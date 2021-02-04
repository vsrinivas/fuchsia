# Running Camera Tests

All camera tests should be run on all current targets before submitting code!

## To test the camera stack:

1. `fx set [product].[board] --with //bundles:tests`
2. You may have to re-flash zedboot if you flashed with a different fx set command
3. `fx serve`
4. Restart the target device in the netboot cable configuration
5. Run `fx test //src/camera`

Note: some on-device tests require the camera to be physically enabled (i.e. the device must not be
muted). If a test determines that the device may be muted, a warning will be displayed and the test
will be skipped.


## To test camera functionality manually:

1. Follow steps 1 through 4 above for on-device test setup
2. Run `fx camera-gym`


## To run single tests:
Single packages can be tested by using the test package's name.  For example:
```
fx test ge2d_task_unittest
```

Alternatively, the absolute path to the test package can be used.  For example:
```
fx test //src/camera/drivers/hw_accel/ge2d
```
