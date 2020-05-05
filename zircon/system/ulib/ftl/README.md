The Flash Translation Layer library provides block management for low-level NAND devices. Not all
flash devices need this as many provide this functionality in their firmware. This directory
contains the FTL library only. It is integrated into the Fuchsia device hierarchy by the separate
FTL driver.

NDM (NAND Device Manager) is the lower layer. It abstracts the different NAND types and error
correction schemes. Theoretically NDM can host multiple volumes on top of it but we do not use it in
this way.

FTLN is the upper-layer that implements the wear leveling and garbage collection.
