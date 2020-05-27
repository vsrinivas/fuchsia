This directory contains C sources that are shared between Fuchsia and
bootloader.

# sysconfig-header.c

The source impelments sysconfig header logic. The header stores
information on the layout of sysconfig partition, including the
offset and size of each sub-partition.

# abr-wear-leveling.c

The source implements logic for performing wear-leveling when writing
new abr metadata to sysconfig partition.

