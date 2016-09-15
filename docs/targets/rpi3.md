
In order to boot on Raspberry Pi3 you will need an sd card with the following:

1. config.txt file containing the following:

`enable_uart=1`
`framebuffer_depth=16`

2. bootcode.bin and start.elf files. Can be obtainied from [here](https://github.com/raspberrypi/firmware/tree/master/boot)

3. copy the magenta.bin file from you build to the sd card as kernel8.img

4. serial console is available on the rpi3 header.
  1. Pin 6 - GND
  2. Pin 8 - TXD (output from Pi)
  3. Pin 10 - RXD (input to pi)
  4. Baudrate = 115200

