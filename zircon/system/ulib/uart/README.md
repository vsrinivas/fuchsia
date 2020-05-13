# Simple UART driver template library

The `lib/uart` template library provides simple driver support for the UART
devices supported as Zircon serial consoles.  Its primary purpose is to
implement serial console support for [phys] executables and the kernel proper,
but it can be used in any environment.  To support [phys] environments, the
code must meet some stringent constraints; see [`lib/arch`] for more details.

The key use case is to instantiate a serial console device in physboot and then
hand it off to the kernel proper.  These are substantially different execution
environments with separate code and limited ability to share data structures.
The library is built around a layer that is hardware-specific but agnostic to
execution environment; and a layer that is hardware-agnostic but parameterized
for a particular execution environment.  The hardware-specific layer's state
can be safely shared or copied between the different environments.

Each supported UART device has a `lib/uart/xyz.h` header file that defines a
`uart::xyz` namespace and a `uart::xyz::Driver` class.  This namespace contains
public [`hwreg`] types for the hardware registers so that user-level driver
code can use the library just for these register definitions even without using
the library's driver support.  The `uart::null::Driver` class in
[`lib/uart/null.h`](include/lib/uart/null.h) providea a bit-bucket fake
hardware device and demonstrates the API contract for the hardware layer.

The "front-end" interface is the `uart::KernelDriver` template class in
[`lib/uart/uart.h`](include/lib/uart/uart.h).  This is parameterized by a
`uart::xyz::Driver` hardware-support class, and two other template parameters
that describe the execution environment's methods for synchronization and
accessing hardware resources (i.e. MMIO and/or PIO).  Trivial implementations
are provided that suffice for [phys] environments.

`uart::all::KernelDriver` in [`lib/uart/all.h`](include/lib/uart/all.h)
provides a variant type fanning out to all the supported device types.  This
can match ZBI items to configure and instantiate the serial console driver.
The hardware configuration and state in the underlying `uart::xyz::Driver`
object can then be transferred from the `uart::all::KernelDriver` instantiation
in one environment to a new instantiation in a different environment.

[`lib/uart/mock.h`](include/lib/uart/mock.h) in the separate `uart-mock`
library provides [`zxtest`] testing support.  `uart::mock::Driver` is used for
testing the front end code itself.  `uart::mock::IoProvider` is used to
instantiate the front end for tests of each hardware-specific driver.

[phys]: ../../../../kernel/phys
[`lib/arch`]: ../../../../kernel/lib/arch
[`hwreg`]: ../../../../system/ulib/hwreg
[`zxtest`]: ../../../../system/ulib/zxtest
