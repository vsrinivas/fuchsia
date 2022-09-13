The registers drivers are used for shared registers across drivers to provide isolation and prevent
race conditions. Often, different drivers want to access different bits on the same register. For
example, often there is are a set of RESET drivers where each bit on the registers resets a
different set of hardware, i.e. bit 0 resets camera related hardware and registers, bit 1 resets
audio related hardware and registers, ... The registers driver allows us to allocate a set of bits
on the register to one driver and another set of bits to another driver, while managing that each
driver is only allowed to touch the bits it was allocated and dealing with any lock contention.

It's also perhaps more common that multiple drivers share different registers on a shared page.
We want to strive to map all MMIO pages in a single driver, and the register driver also provides
us the ability to split these pages into more granular regions.

The mock-registers library can be used for testing.
