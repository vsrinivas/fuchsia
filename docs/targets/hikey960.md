#  Zircon on HiKey960 (96boards.org)
Periodically check this file as the setup workflow will change/improve.


## Requirements

__The following hardware is required:__

+ HiKey960 board
+ Power adapter (most will require a DC plug converter -- more info
  [here](http://www.96boards.org/product/hikey960/))
+ USB-C cable (to connect to workstation for flashing the board)
+ One of the following (to connect to workstation for serial console):
  + (Recommended)
    [Mezzanine board](https://www.seeedstudio.com/96Boards-UART-p-2525.html),
    plus a micro-USB cable (not included with mezzanine board), or
  + (Alternate) [1.8v FTDI Serial Adapter
    cable](https://www.digikey.com/products/en?keywords=768-1070-ND)

__The following software is required:__

+ `fastboot`

  To install on Ubuntu: `sudo apt-get install android-tools-fastboot`

## Overview

At a high level, these are the steps for getting a HiKey development environment
fully working:

+ Build a Zircon boot image
+ Enable the serial console (useful for debugging subsequent steps)
+ Flash the HiKey's low-level firmware
+ Flash the Zircon boot image onto the HiKey (this image -- specifically
  zedboot -- will receive and boot your subsequent Fuchsia builds)
+ Build and boot Fuchsia

Once the system is correctly configured, your development workflow should
resemble a workflow on other hardware (repeated builds done with `fx build`,
a persistent instance of `fx boot` to automatically update the hardware, and a
persistent instance of `fx log` to capture console output).


## Useful Information

+ [HiKey960 Development Board User Manual](https://www.96boards.org/documentation/ConsumerEdition/HiKey960/HardwareDocs/HardwareUserManual.md.html)
+ [96boards-hikey github page](https://github.com/96boards-hikey)
+ [96boards Getting Started page](https://www.96boards.org/documentation/ConsumerEdition/HiKey960/GettingStarted/)
+ [SoC Reference](https://github.com/96boards/documentation/raw/master/ConsumerEdition/HiKey960/HardwareDocs/HiKey960_SoC_Reference_Manual.pdf)
+ [AOSP HiKey960 Information](https://source.android.com/source/devices#hikey960)
+ [HiKey960 Schematic](http://www.lemaker.org/product-hikeysecond-download-62.html)

## Building the Zircon boot image

To build zircon, invoke the following command from the top level Zircon
directory (ensure that you have checked out the ARM64 toolchains). For more
information, see `docs/getting_started.md`:

      make arm64


## Setting up the serial console

First, get the device to show up on your dev host machine as a serial device.
Following that, install and configure a console app.

#### Serial hardware setup

If using a __mezzanine board__, follow the instructions included with it.
Additional tips:

  + Take care not to install the mezzanine board backwards on the connector. The
  micro-USB port should face outward; the corner pushbutton should be in the
  center of the HiKey board.

  + Some standard micro-USB cables have a button to enable/disable the data
  lines. When using one of these cables, ensure that these lines are enabled -
  the LED should be _amber_ (not green).

  + The mezzanine board receives power through the micro-USB cable, so power
  need not be applied to the main HiKey board yet.

If using a __FTDI-style serial adapter cable__:

  + The signals are available on the 40 pin LS connector
([reference](https://raw.githubusercontent.com/96boards/documentation/master/ConsumerEdition/HiKey960/AdditionalDocs/Images/Images_HWUserManual/HiKey960_Numbered_Front2.png))
    + Pin 1  - GND
    + Pin 11 - UART TX (HiKey960 --> Host)
    + PIN 13 - UART RX (HiKey960 <-- Host)


  + This means that for a common [FTDI style adapter](https://www.digikey.com/products/en?keywords=768-1070-ND):
    + Black  --> Pin1
    + Yellow --> Pin11
    + Orange --> Pin13


  + (Optional) an active low reset is available on pin 6 of the 40 pin LS
  connector. A jumper wire intermittently shorted from this pin to GND (shields
  of the connectors are all grounded) can provide an easy way to reset the board
  and place it in fastboot mode.

Once you have correctly configured the hardware (via either method), the device
should appear to your host machine as a USB-connected UART, listed in your /dev
directory as `/dev/ttyUSB0` (or USB1, etc). If this is _not_ the case, you may
have forgotten to enable the data lines (LED should be amber), or you may have a
bad micro-USB cable or mezzanine board. Regardless, do not proceed until your
HiKey board is detected and enumerated in the `/dev` directory as a tty device.

#### Serial console software

Use a host application such as screen or putty to connect to the serial port and
provide console functionality. Use a baud rate of 115200.

Example commands using **screen**:
  + `screen /dev/ttyUSB0 115200,-ixoff`
  + `Ctrl-a, Esc` to enable scrolling (then k-up, j-down, q-done scrolling)
  + `Ctrl-a, d` to detach from the session (`screen -r -d` to reattach)
  + `Ctrl-a, \` to kill all screen sessions.

If you receive an error when connecting to your tty/USBn, you may need to run
your serial console application as `sudo`. Alternately, you can add a udev rule
that allows applications to connect to this device:

  + Create file `/etc/udev/rules.d/99-ttyusb.rules` containing the following:

    `SUBSYSTEM=="tty", GROUP="dialout"`

  + Then `sudo udevadm control --reload-rules`


## Entering fastboot mode
##### (needed for flashing low-level firmware and/or Zircon)

Connect the power supply if you have not already. If the power plug doesn't
seem to fit, you may have forgotten to get a DC adapter. The power plug for the
HiKey boards has a 4.75mm diameter and 1.7mm center pin, whereas most DC power
supplies in this class have a 5.5mm diameter and 2.1mm center pin.

To flash the board, it must be connected to your workstation via the USB-C OTG
connection on the HiKey960 main board. Additionally, the HiKey960 must be in
fastboot mode. You can enter fastboot in one of two ways:

+ __DIP Switch method__  Use the switches on the back of the board. (Older
  HiKeys may have jumpers instead of DIP switches.) To boot into fastboot mode,
  the switches should be in the following positions:

        Auto Power up(Switch 1)   closed/ON
        Recovery(Switch 2)        open/OFF
        Fastboot(Switch 3)        closed/ON

  Once the switches are in these positions, unplug/plug power or reset the
  board. It will then boot into _fastboot_ mode, awaiting commands from the
  host. If you are using the serial adapter cable, just a reminder that this can
  be done with the jumper wire on pin 6, as mentioned earlier.

  Note: after you have performed the last of your flash operations, you want the
  device to boot normally going forward, so you should open (turn OFF) DIP
  switch 3 _before_ your final boot (once firmware and Zircon updates are
  complete, before booting into Zircon for the first time).

+ __Double-Reset method__  Using the button on the mezzanine board, reset the
  board, then reset it _again_ after seeing the following console messages:

        C3R,V0x00000016 e:113
        C0R,V0x00000017 e:66
        C1R,V0x00000017 e:66
        C2R,V0x00000017 e:66
        C3R,V0x00000017 e:66

  The second reset instructs the board to restart into fastboot __for the next
  boot cycle only__. The timing on this double-reset is a little tricky, but you
  will know you got the timing right if you see the following console messages
  at the end of the boot spew:

        usbloader: bootmode is 4
        usb: [USBFINFO]USB RESET
        usb: [USBFINFO]USB CONNDONE, highspeed
        usb: [USBFINFO]USB RESET
        usb: [USBFINFO]USB CONNDONE, highspeed
        usbloader: usb: online (highspeed)
        usb: [USBFINFO]usb enum done

  These messages confirm that the device has restarted into fastboot mode. If
  you do not see these messages, use the button to reset the board and try again
  until you are successful.

  As a reminder, with this method, the DIP switches on the HiKey should remain
  in _normal_ mode (closed/ON open/OFF open/OFF), not the 'fastboot' mode
  mentioned in the previous option.

Once the board is in fastboot mode (regardless of which method you use), it is
ready to be flashed with firmware updates and/or the Zircon boot image.

## Install Firmware

We have run into inconsistent behavior between different HiKey 960 boards,
depending on the low level firmware came installed on the device. We recommend
setting up your board with known good firmware from the Android AOSP project.

To install firmware, put your board in fastboot mode and run the following:

      ./scripts/flash-hikey -f

## Recover the device

If the hikey gets into a bad state you can try the recovery mechanism.
The script should automate the process, including reinstalling the firmware. You
first need to put the device into recovery mode:

        Auto Power up(Switch 1)   closed/ON
        Recovery(Switch 2)        closed/ON
        Fastboot(Switch 3)        open/OFF

Then run:

      ./scripts/flash-hikey -r

The recovery process communicates with the device over the USB-C cable, but it
can be a bit flaky at times. If the script complains that it can't open the
serial device first check what serial devices are connected (`ls
/dev/serial/by-id/`) and make sure the script is using the correct device. You
can specify which serial port to use with `-p`. Sometimes you just need to try a
few times or power cycle the device. Occasionally the script will fail when
attempting to install firmware, which can usually be fixed by starting again.

## Installing Zircon

Once the HiKey board is in fastboot mode, run the following script from the
zircon root directory to flash the necessary files onto the board:

      ./scripts/flash-hikey

## Zedboot

If you would like to boot future kernels via the network, instead of flashing
them directly, then run the script with the `-m` option.

      ./scripts/flash-hikey -m

This is the last flash update, and all subsequent boots should use normal mode
(not fastboot or recovery). If you used the DIP Switch method to place the board
in fastboot mode, you should flip the fastboot switch (switch 3) back to
open/OFF _before_ running this script, so that it will boot into Zircon after
flashing (otherwise, it will boot back into fastboot mode).

If you used the double-tap reset method to place the board into fastboot mode,
no further reconfiguration is needed: the board will boot into the kernel after
it completes flashing.

For now, the ethernet connectivity needed for zedboot is actually provided by
zircon via USB. This is automatically enabled on the HiKey USB-C connector, if
it is changed from host mode into device mode. If all flash steps appear to
complete successfully, but the device does not restart into Zedboot, you may
need to manually place the device into USB 'device' mode. Enter the following
command in your console:

      usbctl mode device

This step must be repeated each time the device is fully powered-down/up. At
some point in the near future, the Fuchsia build will include support for USB
NICs via USB-A, at which time this `usbctl` step will be unnecessary.

Once your device restarts and displays 'Zedboot' in the console, the setup
process is complete. You can now use your usual build, boot and log commands.
When powering up (not simply resetting) the device, you may need to press the
reset button for the device to show up again as /dev/ttyUSBn. Recall that this
is needed before connecting the serial console and interacting with the device.


## Manually Installing Low-Level Firmware

Note: the following requires fastboot in your execution path.

To install firmware, put your board in fastboot mode and run the following:

      git clone https://android.googlesource.com/device/linaro/hikey hikey-firmware
      git -C hikey-firmware checkout 972114436628f874ac9ca28ef38ba82862937fbf
      fastboot flash ptable hikey-firmware/installer/hikey960/ptable.img
      fastboot flash xloader hikey-firmware/installer/hikey960/sec_xloader.img
      fastboot flash fastboot hikey-firmware/installer/hikey960/fastboot.img
      fastboot flash nvme hikey-firmware/installer/hikey960/nvme.img
      fastboot flash fw_lpm3 hikey-firmware/installer/hikey960/lpm3.img
      fastboot flash trustfirmware hikey-firmware/installer/hikey960/bl31.bin

This installs all the AOSP firmware except Android itself. To use a different
bootloader altogether (not the one from AOSP), first complete the above commands
and then install your bootloader.

## Device support

The console is especially important because the HiKey builds of Fuchsia do not
yet support the HDMI port. Related to this, only USB is supported for audio
input/output. NOTE: all USB audio devices must be High-Speed (not just USB 2.0
compatible, which might be Full-Speed or High-Speed). If the USB audio device
is enumerated as 12Mb/s, then it is Full-Speed.
