# Fuchsia Driver Development

Fuchsia drivers are shared libraries that are dynamically loaded in Device Host
processes in user space. The process of loading a driver is controlled by the
Device Coordinator. See [Device Model](device-model.md) for more information on
Device Hosts, Device Coordinator and the driver and device lifecycles.

## Directory structure

Drivers may be found throughout the source tree under `driver` subdirectories of
areas as specified in the
[source code layout](/docs/concepts/source_code/layout.md) document. Most
Fuchsia drivers are found under [//src/devices/](/src/devices). They are grouped
based on the protocols they implement. The driver protocols are defined in
[ddk/include/ddk/protodefs.h](/src/lib/ddk/include/ddk/protodefs.h).
For example, a USB ethernet driver goes in
[//src/connectivity/ethernet/drivers/](/src/connectivity/ethernet/drivers/)
rather than [//src/devices/usb/drivers/](/src/devices/usb/drivers) because it
implements an ethernet protocol. However, drivers that implement the USB stack
are in [//src/devices/usb/drivers/](/src/devices/usb/drivers) because they
implement USB protocols.

In the driver's `BUILD.gn`, there should be a `driver_module` target. In order
to get a driver to show up under `/boot/driver/`, a `migrated_manifest` build
target must be added which should then be added as a dependency inside of
`//build/unification/images:migrated-image`. In order to get it to show up
inside of `/system/driver/` it should be added to the system package via a
`driver_package` build target which should then be referenced by relevant board
file(s) under `//boards/`. The device coordinator looks first in
`/boot/driver/`, then `/system/driver/` for loadable drivers.

## Creating a new driver

Creating a new driver can be done automatically by using the
[Create Tool](/tools/create/README.md). Simply run the following command:

```
fx create driver <NAME> --lang cpp --dest <PATH>
```

This will create an empty driver `<NAME>` at location `<PATH>/<NAME>`. After
this command is run, the following steps need to be followed:

1) Include the `driver_module` or `driver_package` build target in the correct
place to get your driver included into the system.
 - For packaged drivers the `driver_package` build target should be added to
   the relevant board file in `//boards` or `//vendor/<foo>/boards` to a
   `xx_package_labels` GN argument.
 - For boot drivers the `driver_module` build target should be added to
   the relevant board file in `//boards` or `//vendor/<foo>/boards` to the
   `board_bootfs_labels` GN argument.
2) Include the `tests` build target in the `<PATH>:tests` build target to get
your tests included in CQ.
3) Add proper bind rules in `<NAME>.bind`.
4) Write the functionality for the driver.

## Declaring a driver

At a minimum, a driver should contain the driver declaration and implement the
`bind()` driver op.

Drivers are loaded and bound to a device when the Device Coordinator
successfully finds a matching driver for a device. A driver declares the devices
it is compatible with through bind rules, which are should be placed in a
`.bind` file alongside the driver. The bind compiler compiles those rules and
creates a driver declaration macro containing those rules in a C header file.
The following bind program declares the
[AHCI driver](/src/devices/block/drivers/ahci/ahci.h):

```
using deprecated.pci;

deprecated.BIND_PROTOCOL == deprecated.pci.BIND_PROTOCOL.DEVICE;
deprecated.BIND_PCI_CLASS == 0x01;
deprecated.BIND_PCI_SUBCLASS == 0x06;
deprecated.BIND_PCI_INTERFACE == 0x01;
```

These bind rules state that the driver binds to devices with a `BIND_PROTOCOL`
property that matches `DEVICE` from the `pci` namespace and with PCI class 1,
subclass 6, interface 1. The `pci` namespace is imported from the
`deprecated.pci` library on the first line. For more details, refer to the
[binding documentation](driver-binding.md).

To generate a driver declaration macro including these bind rules, there should
be a corresponding `bind_rules` build target.

```
bind_rules("bind") {
    rules = "ahci.bind"
    output = "ahci-bind.h"
    deps = [
        "//src/devices/bind/deprecated.pci",
    ]
}
```

The driver can now include the generated header and declare itself with the
following macro. `"zircon"` is the vendor id and `"0.1"` is the driver version.

```c
#include "src/devices/block/drivers/ahci/ahci-bind.h"
...
ZIRCON_DRIVER(ahci, ahci_driver_ops, "zircon", "0.1");
```

The [PCI driver](/src/devices/bus/drivers/pci/kpci/kpci.c) publishes the
matching device with the following properties:

```c
zx_device_prop_t device_props[] = {
    {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
    {BIND_PCI_VID, 0, info.vendor_id},
    {BIND_PCI_DID, 0, info.device_id},
    {BIND_PCI_CLASS, 0, info.base_class},
    {BIND_PCI_SUBCLASS, 0, info.sub_class},
    {BIND_PCI_INTERFACE, 0, info.program_interface},
    {BIND_PCI_REVISION, 0, info.revision_id},
    {BIND_PCI_BDF_ADDR, 0, BIND_PCI_BDF_PACK(info.bus_id, info.dev_id,
                                             info.func_id)},
};
```

For now, binding variables and macros are defined in
[ddk/binding.h](/src/lib/ddk/include/ddk/binding.h). In the
near future, all bind properties will be defined by bind libraries like the
`deprecated.pci` library imported above. If you are introducing a new device
class, you may need to introduce new bind properties to the binding header as
well as the bind libraries.

Bind properties are 32-bit values. If your variable value requires greater than
a 32-bit value, split them into multiple 32-bit variables. An example is ACPI
HID values, which are 8 characters (64-bits) long. It is split into
`BIND_ACPI_HID_0_3` and `BIND_ACPI_HID_4_7`. Once the migration to bind
libraries is complete you will be able to use other data types such as strings,
larger numbers, and booleans.

Drivers in the ZN build will need to continue to use the old C macro style of
bind rules until the migration is complete. The following is the C macro
equivalent of the bind rules for the AHCI driver.

```c
ZIRCON_DRIVER_BEGIN(ahci, ahci_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x01),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x06),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x01),
ZIRCON_DRIVER_END(ahci)
```

Binding directives are evaluated sequentially. The branching directives
`BI_GOTO()` and `BI_GOTO_IF()` allow you to jump forward to the matching label,
defined by `BI_LABEL()`.

`BI_ABORT_IF_AUTOBIND` may be used (usually as the first instruction) to prevent
the default automatic binding behaviour. In that case, a driver can be bound to
a device using `fuchsia.device.Controller/Bind` FIDL call.

## Driver binding

A driver's `bind()` function is called when it is matched to a device. Generally
a driver will initialize any data structures needed for the device and
initialize hardware in this function. It should not perform any time-consuming
tasks or block in this function, because it is invoked from the devhost's RPC
thread and it will not be able to service other requests in the meantime.
Instead, it should spawn a new thread to perform lengthy tasks.

The driver should make no assumptions about the state of the hardware in
`bind()`, resetting the hardware or otherwise ensuring it is in a known state.
Because the system recovers from a driver crash by re-spawning the devhost, the
hardware may be in an unknown state when `bind()` is invoked.

A driver is required to publish a `zx_device_t` in `bind()` by calling
`device_add()`. This is necessary for the Device Coordinator to keep track of
the device lifecycle. If the driver is not able to publish a functional device
in `bind()`, for example if it is initializing the full device in a thread, it
should publish an invisible device by implementing the device `init()` hook, and
call `device_init_reply()` once initialization is complete.
`device_init_reply()` does not necessarily need to be called from the `init()`
hook. For example, it may be called from another worker thread. The device is
also guaranteed not to be removed until the reply is received. See `init()` in
[src/lib/ddk/include/ddk/device.h](/src/lib/ddk/include/ddk/device.h)
and `device_init_reply()` in
[src/lib/ddk/include/ddk/driver.h](/src/lib/ddk/include/ddk/driver.h).

There are generally four outcomes from `bind()`:

1.  The driver determines the device is supported and does not need to do any
    heavy lifting, so publishes a new device via `device_add()` and returns
    `ZX_OK`.

2.  The driver determines that even though the bind program matched, the device
    cannot be supported (maybe due to checking hw version bits or whatnot) and
    returns an error.

3.  The driver needs to do further initialization before the device is ready or
    it's sure it can support it, so it publishes a device that implements the
    `init()` hook and kicks off a thread to keep working, while returning
    `ZX_OK` to `bind()`. That thread will eventually call `device_init_reply()`
    with a status indicating whether it was able to successfully initialize the
    device and should be made visible, or that the device should be removed.

4.  The driver represents a bus or controller with 0..n children which may
    dynamically appear or disappear. In this case it should publish a device
    immediately representing the bus or controller, and then dynamically publish
    children (that downstream drivers will bind to) representing hardware on
    that bus. Examples: AHCI/SATA, USB, etc.

After a device is added and made visible by the system, it is made available to
client processes and for binding by compatible drivers.

## Device protocols

A driver provides a set of device ops and optional protocol ops to a device.
Device ops implement the device lifecycle methods and the external interface to
the device that are called by other user space applications and services.
Protocol ops implement the ddk-internal protocols of the device that are called
by other drivers.

You can pass one set of protocol ops for the device in `device_add_args_t`. If a
device supports multiple protocols, implement the `get_protocol()` device op. A
device can only have one protocol id. The protocol id corresponds to the class
the device is published under in devfs.

## Driver operation

A driver generally operates by servicing client requests from children drivers
or other processes. It fulfills those requests either by communicating directly
with hardware (for example, via MMIO) or by communicating with its parent device
(for example, queuing a USB transaction).

External client requests from processes outside the devhost are fulfilled by
children drivers, generally in the same process, are fulfilled by device
protocols corresponding to the device class. Driver-to-driver requests should
use device protocols instead of device ops.

A device can get a protocol supported by its parent by calling
`device_get_protocol()` on its parent device.

## Device interrupts

Device interrupts are implemented by interrupt objects, which are a type of
kernel objects. A driver requests a handle to the device interrupt from its
parent device in a device protocol method. The handle returned will be bound to
the appropriate interrupt for the device, as defined by a parent driver. For
example, the PCI protocol implements `map_interrupt()` for PCI children. A
driver should spawn a thread to wait on the interrupt handle.

The kernel will automatically handle masking and unmasking the interrupt as
appropriate, depending on whether the interrupt is edge-triggered or
level-triggered. For level-triggered hardware interrupts,
[zx_interrupt_wait()](/docs/reference/syscalls/interrupt_wait.md) will mask the
interrupt before returning and unmask the interrupt when it is called again the
next time. For edge-triggered interrupts, the interrupt remains unmasked.

The interrupt thread should not perform any long-running tasks. For drivers that
perform lengthy tasks, use a worker thread.

You can signal an interrupt handle with
[zx_interrupt_trigger()](/docs/reference/syscalls/interrupt_trigger.md) on slot
**ZX_INTERRUPT_SLOT_USER** to return from `zx_interrupt_wait()`. This is
necessary to shut down the interrupt thread during driver clean up.

## FIDL Messages

Messages for each device class are defined in the
[FIDL](/docs/development/languages/fidl/README.md) language. Each device
implements zero or more FIDL protocols, multiplexed over a single channel per
client. The driver is given the opportunity to interpret FIDL messages via the
`message()` hook.

## Protocol ops vs. FIDL messages

Protocol ops define the DDK-internal API for a device. FIDL messages define the
external API. Define a protocol op if the function is meant to be called by
other drivers. A driver should call a protocol op on its parent to make use of
those functions.

## Isolate devices

Devices that are added with `DEVICE_ADD_MUST_ISOLATE` spawn a new proxy devhost.
The device exists in both the parent devhost and as the root of the new devhost.
Devmgr attempts to load **driver**`.proxy.so` into this proxy devhost. For
example, PCI is supplied by `libpci.so` so devmgr would look to load
`libpci.proxy.so`. The driver is provided a channel in `create()` when it
creates the proxy device (the "bottom half" that runs in the new devhost). The
proxy device should cache this channel for when it needs to communicate with the
top half (e.g. if it needs to call API on the parent device).

`rxrpc()` is invoked on the top half when this channel is written to by the
bottom half. There is no common wire protocol for this channel. For an example,
refer to the [PCI driver](/src/devices/bus/drivers/pci).

Note: This is a mechanism used by various bus devices and not something general
drivers should have to worry about. (please ping swetland if you think you need
to use this)

## Logging

You can have a driver send log messages to the
[syslog](/docs/development/diagnostics/logs/recording.md) through the use of the
`zxlogf(<log_level>,...)` macro, which is defined in
[ddk/debug.h](/src/lib/ddk/include/ddk/debug.h).

Depending on the type of log level, by default, log messages are sent to the
following logs:

* [syslog](/docs/development/diagnostics/logs/recording.md#logsinksyslog):
  * `ERROR`
  * `WARNING`
  * `INFO`
* [debuglog](/docs/development/diagnostics/logs/recording.md#debuglog_handles):
  * `SERIAL`

To control which log levels are sent to the syslog (other than `SERIAL`), the
[kernel commandline](/docs/reference/kernel/kernel_cmdline.md#drivernamelogflags)
`driver.<driver_name>.log=<level>` can be used. For example,
`driver.sdhci.log=TRACE` additionally enables `DEBUG` and `TRACE` logs for the
sdhci driver, as we are setting a _minimum_ log level, and `TRACE` is lower than
`DEBUG`.

A driver's logs are tagged with the process name, "driver", and the driver name.
This can be used to filter the output of the syslog whilst searching for
particular logs.

For further information on how to view driver logs, see
[viewing logs](/docs/development/diagnostics/logs/viewing.md).

## Driver testing

### Manual hardware unit tests

A driver may choose to implement the `run_unit_tests()` driver op, which
provides the driver a hook in which it may run unit tests at system
initialization with access to the parent device. This means the driver may test
its bind and unbind hooks, as well as any interactions with real hardware. If
the tests pass (the driver returns `true` from the hook) then operation will
continue as normal and `bind()` will execute. If the tests fail then the device
manager will assume that the driver is invalid and never attempt to bind it.

Since these tests must run at system initialization (in order to not interfere
with the usual operation of the driver) they are activated via a
[kernel command line flag](/docs/reference/kernel/kernel_cmdline.md). To enable
the hook for a specific driver, use `driver.<name>.tests.enable`. Or for all
drivers: `driver.tests.enable`. If a driver doesn't implement `run_unit_tests()`
then these flags will have no effect.

`run_unit_tests()` passes the driver a channel for it to write test output to.
Test output should be in the form of `fuchsia.driver.test.Logger` FIDL messages.
The driver-unit-test library contains a [helper class][] that integrates with
zxtest and handles logging for you.

[helper class]: /zircon/system/ulib/driver-unit-test/include/lib/driver-unit-test/logger.h

### Integration tests

Driver authors can use several means for writing integration tests. For simple
cases, the [fake-ddk](/src/devices/testing/fake_ddk) library is recommended. For
more complicated ones,
[driver-integration-test](/zircon/system/ulib/driver-integration-test) is
recommended.

TODO(fxbug.dev/51320): Fill out more detail here.

## Driver rights

Although drivers run in user space processes, they have a more restricted set of
rights than normal processes. Drivers are not allowed to access the filesystem,
including devfs. That means a driver cannot interact with arbitrary devices. If
your driver needs to do this, consider writing a service instead. For example,
the virtual console is implemented by the [virtcon](/src/bringup/bin/virtcon)
service.

Privileged operations such as `zx_vmo_create_contiguous()` and
[zx_interrupt_create](/docs/reference/syscalls/interrupt_create.md) require a
root resource handle. This handle is not available to drivers other than the
system driver ([ACPI](/src/devices/board/drivers/x86) on x86 systems and
[platform](/src/devices/bus/drivers/platform) on ARM systems). A device should
request its parent to perform such operations for it. Contact the author of the
parent driver if its protocol does not address this use case.

Similarly, a driver is not allowed to request arbitrary MMIO ranges, interrupts
or GPIOs. Bus drivers such as PCI and platform only return the resources
associated to the child device.
