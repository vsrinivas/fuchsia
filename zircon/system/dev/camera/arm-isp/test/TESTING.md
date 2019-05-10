# Running Arm-ISP Tests

As of 5/7/2019, the way to run the ARM ISP test is as follows:

1. `fx set smart_display.sherlock --with-base //garnet/packages/tests:zircon`
2. You may have to re-flash zedboot if you flashed with a different fx set command
3. `fx serve`
4. Restart your Sherlock device in the netboot cable configuration
5. Run `fx shell system/test/sys/arm-isp-test`
