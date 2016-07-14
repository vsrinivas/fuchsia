# FAT Filesystem

This directory contains an (in-development) implementation of the FAT16 / FAT32 filesystem.
At the moment, FAT12 is unsupported.

For a more complete reference, refer to a [formal specification of
FAT](https://staff.washington.edu/dittrich/misc/fatgen103.pdf).

## [Boot Sector](bootrecord)

The bootsector of the FAT filesystem acts like a superblock, and provides access
to filesystem metadata. This sector confirms that the FAT volume is properly
formatted, reveals if the volume is FAT16, FAT32, or something else, identifies
locations of clusters on the block device, and identifies other filesystem
metadata.

Reading the boot sector is one of the first steps required when mounting a FAT
filesystem.
