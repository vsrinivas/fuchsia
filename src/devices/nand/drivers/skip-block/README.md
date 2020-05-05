The skip-block driver is a layer on top of a raw NAND device that will skip bad blocks, but provides
no higher-level processing. Compared to the more advanced FTL scheme, skip-block provides no wear
leveling or garbage collection and always updates entire blocks

Skip-block partitions (one kind of partition tracked by the "nandpart" driver) are used when data
needs to be accessible by the bootloader. The bootloader doesn't understand the more advanced FTL
partitions and this data isn't written frequently enough for performance or wear-leveling to be an
issue.
