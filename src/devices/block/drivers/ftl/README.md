This FTL driver exposes the FTL (Flash Translation Layer) library as a driver. FTL is used to manage
wear leveling and bad block management for NAND devices.

The BlockDevice class implements Fuchsia's BlockDevice protocol and forwards requests to the FTL
library. The FTL library in turn uses the interface implemented by the NandDriver class to talk to
the underlying Fuchsia NAND device.
