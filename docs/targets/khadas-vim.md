# Zircon on Khadas VIM and VIM2 Boards

This document describes running Zircon on the Khadas VIM and VIM2 boards.
These two boards are very similar so one document can describe how to work with both of them.
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

In [this diagram](http://docs.khadas.com/basics/VimGPIOPinout/) of the 40 pin header,
these correspond to pins 17 through 19.

## Buttons

The VIM and VIM2 have 3 buttons on the left side of the board.
On the board schematic, SW1 (also labelled 'R') is the switch closest to the USB
plug. This is the reset switch. The other two switches are general purpose,
SW3 (farthest away from the USB plug, labelled P on the schematic) can
be used for entering flashing mode.  If SW3 is held down while the
board is reset or power cycled, the bootloader will enter flashing mode
instead of booting the kernel normally.

## Preparing the Bootloader

The VIM boards come preinstalled with a u-boot bootloader.
By default the bootloader is configured to use a proprietary Amlogic protocol
for flashing the board.
Unfortunately the Amlogic "update" flashing tool is not widely available
and only runs on Linux. So we will configure the bootloader to use the fastboot
protocol instead.

To configure u-boot to use fastboot, reset the board and repeatedly press the
space bar in the serial console. This should get you into u-boot's shell:

```
kvim#
```

In the u-boot shell, type:

```
setenv update fastboot
saveenv
```

After resetting the board again, the board should enter fastboot mode when booting
if you press SW3 for long enough (first you will see "detect upgrade key" on the
serial console and if you hold it longer, you will see "USB RESET/SPEED ENUM).
At this point, the board can be flashed with the Android fastboot tool.  
If the board fails to enter fastboot mode, your board might have an older version of u-boot
that does not support it.  
In that case you will need to update the u-boot on your board.
Otherwise, skip ahead to "Building Zircon"

The steps are described [here](http://docs.khadas.com/develop/BuildAndroid/),
but below are some simplified instructions that do not assume you checked out a full Android
source tree, and use the Amlogic "update" tool instead of tftp:

```
# install toolchains:
sudo apt install gcc-aarch64-linux-gnu
sudo apt install gcc-arm-none-eabi

# get the u-boot sources:
git clone https://github.com/khadas/u-boot
cd u-boot
git checkout origin/Nougat

# for VIM
make kvim_defconfig
# for VIM2
make kvim2_defconfig

make -j8 CROSS_COMPILE=aarch64-linux-gnu-

# flash the new u-boot using the Amlogic "update" tool:
update partition bootloader fip/u-boot.bin
```
## Building Zircon

```
make -j32 arm64
```

## Flashing Zircon

First enter fastboot mode by resetting the board with SW3 depressed. If you want
to flash zedboot instead of zircon, please add '-m' on the command line.
Then:

### VIM

```
scripts/flash-vim [-m]
```

### VIM2

```
scripts/flash-vim2 [-m]
```

### netbooting

```
zircon: ./build-x86/tools/bootserver ./build-arm64/zircon.bin ./build-arm64/vim2-bootdata.bin
garnet: fx set x64 --netboot; fx build; fx boot vim2
```

### Fuchsia logo

To update the the boot splash screen to be the Fuchsia logo, do this in fastboot mode:
```
fastboot flash logo kernel/target/arm64/board/vim2/firmware/logo.img
```
