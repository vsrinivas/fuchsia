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

## Library API

### Device-specific API Layer

Each supported UART device has a `lib/uart/xyz.h` header file that defines a
`uart::xyz` namespace and a `uart::xyz::Driver` class.  This namespace contains
public [`hwreg`] types for the hardware registers so that user-level driver
code can use the library just for these register definitions even without using
the library's driver support.  The `uart::null::Driver` class in
[`lib/uart/null.h`](include/lib/uart/null.h) provides a bit-bucket fake
hardware device and demonstrates the API contract for the hardware layer.

### Generic API Layer

The "front-end" interface is the `uart::KernelDriver` template class in
[`lib/uart/uart.h`](include/lib/uart/uart.h).  This is parameterized by a
`uart::xyz::Driver` hardware-support class, and two other template parameters
that describe the execution environment's methods for synchronization and
accessing hardware resources (i.e. MMIO and/or PIO).  Trivial implementations
are provided that suffice for [phys] environments.

### Runtime Driver-Selection API Layer

`uart::all::KernelDriver` in [`lib/uart/all.h`](include/lib/uart/all.h)
provides a variant type fanning out to all the supported device types.  This
can match ZBI items to configure and instantiate the serial console driver.
The hardware configuration and state in the underlying `uart::xyz::Driver`
object can then be transferred from the `uart::all::KernelDriver` instantiation
in one environment to a new instantiation in a different environment.

### Unit Test Support

[`lib/uart/mock.h`](include/lib/uart/mock.h) in the separate `uart-mock`
library provides [`zxtest`] testing support.  `uart::mock::Driver` is used for
testing the front end code itself.  `uart::mock::IoProvider` is used to
instantiate the front end for tests of each hardware-specific driver.

## Adding a New Driver

For each UART device, or family of related UART devices, there is one header
file, as described above: `lib/uart/xyz.h` for the "xyz" UART device family.
Some examples of existing drivers for common UARTs include
[`lib/uart/pl011.h`](include/lib/uart/pl011.h) and
[`lib/uart/ns8250.h`](include/lib/uart/ns8250.h).

### Hardware Register Types

First, simply define the hardware's register layout details using `hwreg` types
in the `uart::xyz` namespace.  The register details and canonical names for bit
fields are usually found in the manufacturer's data sheet.  These parts of the
header may also be used for user-level driver code or other test code for the
same hardware interfaces that doesn't use this library's driver logic.  So they
should generally use the hardware specification's vocabulary and be as complete
as possible.

### ZBI Item Protocol

Each kind of UART device is described to the Zircon kernel by a boot loader via
a ZBI item of type `ZBI_TYPE_KERNEL_DRIVER`.  The `extra` header field is the
subtype, a `ZBI_KERNEL_DRIVER_*_UART` constant defined in
[`<zircon/boot/driver-config.h>`](../../../system/public/zircon/boot/driver-config.h).
That file is generated from the FIDL source files in
[`//sdk/fidl/zbi`](../../../sdk/fidl/zbi), where the `KernelDriver` `enum` type
represents all these constants.

For a new driver, a new unique constant must be added there.  It's a good idea
for the name to match the driver header file and namespace name (upcased).

Each specific UART subtype also indicates the ZBI item payload type that's used
to carry information from the boot loader about how the particular device is
accessed, such as MMIO address and interrupt routing details.  Various
`zbi_dcfg_*_t` types are defined to carry necessary information.  Most UART
devices use `zbi_dcfg_simple_t`.  Choose the type that best fits the need of
the new device, and add a new type if necessary.  (If you need to add a type,
there are some more pieces to add to the generic library code; look for the
functions specialized on `dcfg_simple_t` for the models.)

The `ZBI_KERNEL_DRIVER_*_UART` constant and the `zbi_dcfg_*_t` payload type are
template parameters used when defining the `uart::xyz::Driver` type in the new
`<lib/uart/xyz.h>`:

```c++
struct Driver : public DriverBase<Driver, ZBI_KERNEL_DRIVER_XYZ_UART, zbi_dcfg_simple_t> { ... };
```

### Add Unit Tests

Add a `test/xyz-tests.cc` file and add it to the list in `test/BUILD.gn`.
These tests can use the [`lib/uart/mock.h`](include/lib/uart/mock.h) API to
test the MMIO (or PIO) reads and writes the driver should be doing.  See the
existing tests for examples.

### Add to `uart::all`

Finally, add the `xyz::Driver` type to the `WithAllDrivers` template in
[`lib/uart/all.h`](include/lib/uart/all.h).  This lists all the drivers that
will be built into the Zircon kernel.  If the new driver is for a device that
really only ever exists on systems of a certain CPU architecture, then it can
go into one of the `#if defined(__cpuname__) || UART_ALL_DRIVERS` blocks.

### Using the New Driver

The Zircon kernel chooses the driver and configuration for its serial console
in one of two ways:
 * `ZBI_TYPE_KERNEL_DRIVER` items with subtype `ZBI_KERNEL_DRIVER_XYZ_UART`
 * `kernel.serial=xyz,...` boot option (how the `...` is parsed varies for
   each `dcfg_*_t` type, e.g. `xyz,0x1234000,12` for `dcfg_simple_t` giving
   an MMIO address in hex and an IRQ number in decimal)
Both of these can be supplied directly by the bootloader, or they can
be embedded in a ZBI at build time or manually using the `zbi` tool.

Among all ZBI items with supported UART subtypes, the last in the ZBI item
sequence will be used (items synthesized by the bootloader appear after the
items embedded in the ZBI file).  If a `kernel.serial=...` boot option appears
in a command-line string, it will always override any ZBI items with UART
subtypes.  If multiple `kernel.serial=...` strings appear, the last one will be
used (command-line strings can also be embedded in the ZBI, and those also
precede and are overridden by any strings added by the bootloader).

### Custom `phys` tests

For the very first real-world test, there may not be any ZBI-compatible
bootloader already available on your platform.  In this case, you can start
with a raw [`phys` test](../../../kernel/phys/test) that is hard-wired for
your particular configuration, e.g.

```c++
#include <lib/uart/xyz.h>
#include <stdio.h>
#include <stdlib.h>
#include <phys/main.h>
#include <phys/stdio.h>
#include <phys/uart.h>

void PhysMain(void* ptr, arch::EarlyTicks ticks) {
  ApplyRelocations();

  InitStdout();

  constexpr dcfg_simple_t kXyzConfig = {.mmio_phys=0x1234000};
  static uart::xyz::KernelDriver<> uart(kXyzConfig);
  SetUartConsole(uart.uart());

  printf("hello world!\n");

  abort();
}
```

This can be wired up with build rules in
[`kernel/phys/test/BUILD.gn`](../../../kernel/phys/test/BUILD.gn) as a
`phys_executable()` target e.g. using
`//zircon/kernel/arch/arm64/phys:linuxboot` to make an image that's bootable as
a Linux/ARM64 kernel with any bootloader supporting that traditional format.

Once this is proven to work, you should be ready to move on either to building
a [boot shim] or to booting Zircon directly.

[phys]: ../../../kernel/phys
[boot shim]: ../../../kernel/phys/boot-shim
[`lib/arch`]: ../../../kernel/lib/arch
[`hwreg`]: ../../../system/ulib/hwreg
[`zxtest`]: ../../../system/ulib/zxtest
