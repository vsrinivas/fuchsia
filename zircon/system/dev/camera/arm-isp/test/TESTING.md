# Running Arm-ISP Tests

As of 5/7/2019, the way to run the ARM ISP test is as follows:
## To test the on-device functionality:
1. `fx set smart_display.sherlock --with-base //garnet/packages/tests:zircon`
2. You may have to re-flash zedboot if you flashed with a different fx set command
3. `fx serve`
4. Restart your Sherlock device in the netboot cable configuration
5. Run `fx shell system/test/sys/arm-isp-on-device-test`

## To test associated isp libraries off-device:
You can follow the directions above to test on the device, and run:
`fx shell system/test/sys/arm-isp-test`

Or:

1. `fx set workstation.x64 --with-base //garnet/packages/tests:zircon`
2. `fx build`
3. `fx serve`
4. `fx run -k -N`
5. In that terminal: `system/test/sys/arm-isp-test`
