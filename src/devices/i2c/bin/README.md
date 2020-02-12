I2C Utility
===========

Provides I2C devices writes and reads.

1. `i2cutil r[ead] DEVICE ADDRESS`

   Reads one byte from the specified `ADDRESS` of the specified `DEVICE` devfs node and
   outputs to the console. `ADDRESS` here refers to the internal register set of the device, the
   I2C address is already part of the `DEVICE` we are communicating with. Examples:
   a) `i2cutil /dev/class/i2c/004 r 5`, reads one byte from the one byte address 0x05.
   b) `i2cutil /dev/class/i2c/004 r 0x20 0x3d`, reads one byte from the two byte address 0x203d.
   This command is a convenience command, it is equivalent to a `t` command (transaction, see
   below) with 2 segments, one that writes the address bytes and one that reads one byte back.

2. `i2cutil w[rite] DEVICE DATA...`

   Writes `DATA` bytes into the specified I2C `DEVICE` devfs node and outputs to the console.
   Depending on the device, the first byte(s) correspond to the devices' internal address. Examples:
   a) `i2cutil /dev/class/i2c/004 w 0x05 0x12`, writes byte 0x12 to address 0x05.
   b) `i2cutil /dev/class/i2c/004 w 0x20 0x3d 0x80`, writes byte 0x80 to address 0x203d.
   This command is a convenience command, it is equivalent to a `t` command (transaction, see
   below) with 1 segment that writes the specified bytes.

3. `i2cutil t[ransact] DEVICE [w|r] [DATA...|LENGTH] [w|r] [DATA...|LENGTH]`

   Performs one transaction with multiple segments. Each segment may be of type `w` (write) or `r`
   (read). For each write `DATA` bytes are written into the specified I2C `DEVICE` devfs node and
   for each read `LENGTH` bytes are read from the specified I2C `DEVICE` devfs node.
   Outputs to the console the read bytes.
   Examples:
   `i2cutil /dev/class/i2c/000 t w 5 r 1` (equivalent to example a) within section 1 above).
   `i2cutil /dev/class/i2c/000 t w 0x20 0x3d 0x80` (equivalent to example b) within 2 above).
