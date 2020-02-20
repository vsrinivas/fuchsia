SPI bus utility
===============

1. `spiutil DEVICE r LENGTH`

   Read `LENGTH` bytes from the specified devfs node. Output will be on the
   console in hexdump format.

2. `spiutil DEVICE w BYTES ...`

   Write `BYTES` to the SPI bus represented by the specified devfs node.

3. `spiutil DEVICE x BYTES ...`

   Perform a full-duplex SPI exchange on the SPI bus represented by the
   specified devfs node. Transmitted data as `w` option above, output as `r`
   option.
