# USB Peripheral Test

## Overview

This directory contains a test for USB peripheral role functionality in Zircon.
It consists of two parts - a Zircon USB function driver and a Linux host test program.
The host test (called usb-peripheral-test) tests control requests in both the IN and OUT
direction with various payload sizes, receiving data on an interrupt endpoint and finally
bulk transfers.

## How to Run

First of all, this test only works with devices that implement USB peripheral role functionality.
The [hikey960 board](../../../../docs/targets/hikey960.md) is one of those devices, so here we
assume you are using a hikey960 board. To run the test, follow these steps:

1. Connect the Hikey USB-C port to your Linux desktop

2. In the Hikey's shell, switch to USB peripheral mode and enable the test driver:
```
usbctl mode peripheral
usbctl peripheral reset
usbctl peripheral init-test
```

3. Run the host test:

```
out/default.zircon/host-x64-linux-clang/obj/system/dev/usb/usb-peripheral-test/usb-peripheral-test
```

If the test succeeds, you should see something like:
```
CASE usb_peripheral_tests                               [STARTED]
    control_interrupt_test_8                            [RUNNING] [PASSED] (36 ms)
    control_interrupt_test_64                           [RUNNING] [PASSED] (31 ms)
    control_interrupt_test_100                          [RUNNING] [PASSED] (31 ms)
    control_interrupt_test_256                          [RUNNING] [PASSED] (31 ms)
    control_interrupt_test_1000                         [RUNNING] [PASSED] (31 ms)
    bulk_test                                           [RUNNING] [PASSED] (19 ms)
CASE usb_peripheral_tests                               [PASSED]
```

## TODO

Future versions of this test may include:
* isochronous transfers
* setting and clearing stall conditions
