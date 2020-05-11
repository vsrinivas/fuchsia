ram-info
===============

The ram-info tool can be used to report RAM bandwidth measurements on platforms that support it. The
tool will attempt to auto-detect the devfs device to use.

Arguments
---------------

- `--channels <list of port masks>` or `-c <list of port masks>`

  Use the specified list of port masks instead of the device defaults. Each bit in a mask represents
  a port to enable for the channel. Up to eight channels can be used, and each mask holds up to 64
  bits. Masks are separated by commas, and can be decimal or hexadecimal using the `0x` prefix. The
  meaning of each port is device-dependent.

  Example: `ram info --channels 0x1234,5678,99,0x100000`

- `--csv`

  Print bandwidth measurements in CSV format instead of the default format.
