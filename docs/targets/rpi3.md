#  Magenta on Raspberry Pi 3

Magenta is a 64-bit kernel that is capable of running on the Raspberry Pi 3.

Presently it supports a number of the Raspberry Pi 3's peripherals including
the following:
 + USB (Keyboard, Mouse, Flash Drives)
 + Ethernet
 + Software Driven HDMI
 + Serial Port (MiniUART)

The following peripherals are not yet supported:
 + Wi-Fi
 + Hardware Accelerated Graphics
 + Bluetooth

## Requirements

The following hardware is required:
 + Raspberry Pi 3
 + MicroSD Card (at least 32MB suggested)
 + Micro USB cable for power
 + At least one of the following for Input/Output
    - 3.3v FTDI Serial Dongle (recommended, especially for low-level hacking)
    - USB Keyboard / HDMI Monitor

## Building
To build magenta, invoke the following command from the top level Magenta
directory (ensure that you have checked out the ARM64 toolchains). For more
information, see `docs/getting_started.md`:

    make magenta-rpi3-arm64

## Installing
1. To install Magenta, ensure that your SD is formatted as follows:
   + Using an MBR partition table
   + With a FAT32 boot partition

2. Invoking `make magenta-rpi3-arm64` should have created files `magenta.bin`
   and `bootdata.bin` the following path `./build-magenta-rpi3-arm64/`


3. Copy the `magenta.bin` file to the SD card's boot partition as `kernel8.img`
   as follows:

        cp ./build-magenta-rpi3-arm64/magenta.bin <path/to/sdcard/mount>/kernel8.img

4. Copy the `bootdata.bin` file to the SD card's boot partition as follows:

        cp ./build-magenta-rpi3-arm64/bootdata.bin <path/to/sdcard/mount>/bootdata.bin

5. You must also copy `bootcode.bin` and `start.elf` to the boot partition. They
   can be obtained from [here](https://github.com/raspberrypi/firmware/raw/7fcb39cb5b5543ca7485cd1ae9e6d908f31e40c6/boot/bootcode.bin) and [here](https://github.com/raspberrypi/firmware/raw/390f53ed0fd79df274bdcc81d99e09fa262f03ab/boot/start.elf) respectively.

6. Copy `config.txt` `cmdline.txt` and `bcm2710-rpi-3-b.dtb` from
   `./kernel/target/rpi3/` to the boot partition:

         cp ./kernel/target/rpi3/config.txt <path/to/sdcard/mount>/config.txt
         cp ./kernel/target/rpi3/cmdline.txt <path/to/sdcard/mount>/cmdline.txt
         cp ./kernel/target/rpi3/bcm2710-rpi-3-b.dtb <path/to/sdcard/mount>/bcm2710-rpi-3-b.dtb

   It is imperative that these files are named exactly as listed when copied to
   the SD card.

7. At this point your SD Card should be formatted with an MBR partition table
   and FAT32 boot partition that contains the following 7 files:
   + bootcode.bin
   + bootdata.bin
   + config.txt
   + kernel8.img
   + start.elf
   + bcm2710-rpi-3-b.dtb
   + cmdline.txt

8. If you're using the Serial Console, connect your serial dongle to the RPi3
   header as follows:
   1. Pin 6 - GND
   2. Pin 8 - TXD (output from Pi)
   3. Pin 10 - RXD (input to pi)
   4. Baudrate = 115200

9. Insert the SD Card and connect power to boot the Pi

## Netboot
Add `netsvc.netboot=true` to `cmdline.txt` to enable netbooting from the SD Card.
