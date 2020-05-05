The NAND driver is a simple layer on top of a device-specific rawnand driver. It adds an
asynchronous interface with retry logic and will split large requests into the smaller per-page
requests required by the rawnand driver.
