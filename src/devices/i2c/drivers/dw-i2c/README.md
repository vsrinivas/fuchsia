# Testing dw-i2c on as370 hardware

Follow these steps to test the driver:

1. Enable the test flag (``I2C_AS370_DW_TEST``) in the source file before compiling.
2. Build and flash as370
```bash
    fx set bringup.as370
    fx build
    fx vendor google flash-as370 zedboot
```
3. *Success*: Once booted, look for ``DW I2C test for AS370 passed`` in the kernel logs.
4. *Failure*: The test should ideally be completed immediately upon bootup. In case of any error or timeout, ``DW I2C test for AS370 failed`` will be printed within couple of seconds.
