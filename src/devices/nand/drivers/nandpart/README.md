Nandpart is a low-level partitioning scheme on top of a NAND device. These devices can't use the
more-common GPT partitioning scheme.

It allows different parts of the disk to use different NAND management strategies such as skip-block
(only tracks bad blocks) or FTL (provides wear leveling, etc.). The FVM holds the normal filesystems
inside an FTL-managed partition.

Normally, the partition information used by nandpart is provided by the bootloader.
