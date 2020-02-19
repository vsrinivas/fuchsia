SDIO tool
===============

The SDIO tool can be used to read or write registers on the card, stress test/benchmark the bus, or
print information about the card and controller.

Commands
---------------

The SDIO tool takes an SDIO function device path as the first argument, e.g. `/dev/class/sdio/000`.
The second argument is the name of the command to invoke (listed below). All read and write commands
take a numerical card address as the third argument. The width of this field is 17 bits, so the
address must be in the range [0, 0x1ffff]. Numerical fields may be specified in hexadecimal (`0x`
prefix) or decimal (no prefix).

- `sdio <device> info`

  Prints everything we know about the controller and the card.

- `sdio <device> read-byte <address>`

  Reads one byte from the given address and prints it in hexadecimal.

- `sdio <device> write-byte <address>`

  Writes one byte to the given address.

- `sdio <device> read-stress <address> <size> <loops> [--fifo] [--dma]`

  Reads `loops` chunks of data from the given address, where each chunk is `size` bytes. The data
  may be read from a fixed address (`--fifo`) and may be done using DMA (`--dma`). At the end,
  statistics about the transfer (total time and throughput) are printed.
