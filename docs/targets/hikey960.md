#  Magenta on 96boards.org HiKey960

## Requirements

1. The following hardware is required:
  + HiKey960 + Power Adapter (Look [here](http://www.96boards.org/product/hikey960/) for more info)
  + USB-C cable to connect to workstation for purpose of flashing
  + [1.8v FTDI Serial adapter](https://www.digikey.com/products/en?keywords=768-1070-ND) for console
  + Suggested: [mezzanine board](https://www.seeedstudio.com/96Boards-UART-p-2525.html) (eliminates need for 1.8v serial adapter)

2. The following software is required:
 + `fastboot` Can be installed on Ubuntu via:

      `sudo apt-get install android-tools-fastboot`

 + `mkbootimg`, `mkdtimg`. It is suggested to use the version of these tools available in the `build-from-source` dirctory of the 96boards-hikey tools repo on github.  Both of these tools will need to be in your execution path.

      `git clone https://github.com/96boards-hikey/tools-images-hikey960.git`


## Useful Information
+ [HiKey960 Development Board User Manual](http://www.96boards.org/documentation/ConsumerEdition/HiKey960/HardwareDocs/HardwareUserManual.md/)
+ [96boards-hikey github page](https://github.com/96boards-hikey)
+ [96boards Getting Started page](http://www.96boards.org/documentation/ConsumerEdition/HiKey960/GettingStarted/README.md/)
+ [SoC Reference](https://github.com/96boards/documentation/raw/master/ConsumerEdition/HiKey960/HardwareDocs/HiKey960_SoC_Reference_Manual.pdf)
+ [AOSP HiKey960 Information](https://source.android.com/source/devices#hikey960)

## Building
To build magenta, invoke the following command from the top level Magenta
directory (ensure that you have checked out the ARM64 toolchains). For more
information, see `docs/getting_started.md`:

      make magenta-hikey960-arm64

## Setup
Periodically check this file as the setup workflow will change/improve.

If using the mezzanine board, follow the instructions included with the board.

If using a FTDI style serial adapter:

1. The signals are available on the 40 pin LS connector ([reference](https://raw.githubusercontent.com/96boards/documentation/master/ConsumerEdition/HiKey960/AdditionalDocs/Images/Images_HWUserManual/HiKey960_Numbered_Front2.png))

      Pin 1  - GND
        Pin 11 - UART TX (HiKey960 -> Host)
        PIN 13 - UART RX (Hikey960 <- Host)

    So for a common [FTDI style adapter](https://www.digikey.com/products/en?keywords=768-1070-ND):

        Black  -> Pin1
        Yellow -> Pin11
        Orange -> Pin13

2. Optional - If not using the mezzanine board, an active low reset is avalable on pin 6 of the 40 pin LS connector.  A jumper wire intermittently shorted from this pin to GND (shields of the connectors are all grounded) can provide an easy way to reset the board and place in fastboot mode.



## Installing

In order to flash the board, it will need to be connected to your workstation via the USB-C OTG connection on the HiKey960.

To install Magenta, the HiKey960 will need to be placed in fastboot mode.  This can be done in one of two ways:

1. Using the DIP switches on the back of the board.  To place in fastboot mode the switches should be in the following positions:

        Auto Power up(Switch 1)   closed/ON
        Recovery(Switch 2)        open/OFF
        Fastboot(Switch 3)        closed/ON

    Once the switches are in these positions, either cycle power or reset the board and it will boot into fastboot mode, awaiting for commands from the host.  This can be done with the jumper wire on pin 6 mentioned earlier in these instructions.

2. If the board is reset, then reset again immediately after seeing the the following messages on the console:

        C3R,V0x00000016 e:113
        C0R,V0x00000017 e:66
        C1R,V0x00000017 e:66
        C2R,V0x00000017 e:66
        C3R,V0x00000017 e:66

    The second reset should allow it to boot into fastboot, which is indicated by the following messages at the end of the boot spew:

        usbloader: bootmode is 4
        usb: [USBFINFO]USB RESET
        usb: [USBFINFO]USB CONNDONE, highspeed
        usb: [USBFINFO]USB RESET
        usb: [USBFINFO]USB CONNDONE, highspeed
        usbloader: usb: online (highspeed)
        usb: [USBFINFO]usb enum done

Once the board is in fastboot, the following script can be run from the magenta root director to flash the necessary files onto the board:

        ./scripts/flash-hikey

The board will reboot after flashing.  If the DIP switch method was used to place the board in fastboot mode, it is a good idea to flip the fastboot switch (switch 3) back to open/OFF before running the script so that it will boot into the kernel after flashing and not back into fastboot mode.

If the double tap reset method was used to place the board in fastboot mode, then no further action is needed and the board will boot into the kernel after it has completed flashing.