# Zircon on Khadas VIM2 Board

This document describes running Zircon on the Khadas VIM2 board.
Additional documentation can be found at [docs.khadas.com](http://docs.khadas.com/)

When describing the location of buttons, pins and other items on the board,
we will refer to the side with the USB, ethernet and HDMI connectors as the front of the board
and the opposite side the back of the board.

## Heat Sink

Before you start, you need a heat sink. A passive chip heat sink will allow you
to run 2 cores out of 8 at full speed before reaching 80C, the critical
temperature at which cores have to be throttled down.


## Serial Console

The debug UART for the serial console is exposed on the 40 pin header at the back of the board.
You may use a 3.3v FTDI USB to serial cable to access the serial console.
On the front row of the header:

- 2nd from right: TX (Yellow wire)
- 3rd from right: RX (Orange wire)
- 4th from right: Ground (Black wire)

For FTDI serial cables with black, white red and green wires, use this:

- 2nd from right: TX (White wire)
- 3rd from right: RX (Green wire)
- 4th from right: Ground (Black wire)

In [this diagram](http://docs.khadas.com/basics/VimGPIOPinout/) of the 40 pin header,
these correspond to pins 17 through 19.

## Buttons

The VIM2 has 3 buttons on the left side of the board.
On the board schematic, SW1 (also labelled 'R') is the switch closest to the USB
plug. This is the reset switch. The other two switches are general purpose,
SW3 (farthest away from the USB plug, labelled P on the schematic) can
be used for entering flashing mode.  If SW3 is held down while the
board is reset or power cycled, the bootloader will enter flashing mode
instead of booting the kernel normally.

## VIM2 Bootloader

Booting Zircon on the VIM2 requires a custom bootloader.
Within Google, this can be found at [go/vim2-bootloader](http://go/vim2-bootloader).
If you are not at Google, hang on until we make this publicly available.

To find out what version of the bootloader you have, grep for "fuchsia-bootloader"
in the kernel boot log. You should see something like: "cmdline: fuchsia-bootloader=0.04"

## Building Zircon

```
make -j32 arm64
```

## Flashing Zircon

First enter fastboot mode by resetting the board with SW3 depressed. If you want
to flash zedboot instead of zircon, please add '-m' on the command line.
Then:

```
scripts/flash-vim2 [-m]
```

### netbooting

```
zircon: ./build-x86/tools/bootserver ./build-arm64/zircon.bin ./build-arm64/vim2-bootdata.bin
garnet: fx set x64 --netboot; fx build; fx boot
```

### Fuchsia logo

To update the the boot splash screen to be the Fuchsia logo, do this in fastboot mode:
```
fastboot flash logo kernel/target/arm64/board/vim2/firmware/logo.img
```
