# Zircon Driver Development

Zircon drivers are shared libraries that are dynamically loaded in Device Host
processes in user space. The process of loading a driver is controlled by the
Device Coordinator. See [Device Model](device-model.md) for more information on
Device Hosts, Device Coordinator and the driver and device lifecycles.

## Directory structure

Zircon drivers are found under [system/dev](../../system/dev).
They are grouped based on the protocols they implement.
The driver protocols are defined in
[ddk/include/ddk/protodefs.h](../../system/ulib/ddk/include/ddk/protodefs.h).
For example, a USB ethernet driver goes in [system/dev/ethernet](../../system/dev/ethernet)
rather than [system/dev/usb](../../system/dev/usb) because it implements an ethernet protocol.
However, drivers that implement the USB stack are in [system/dev/usb](../../system/dev/usb)
because they implement USB protocols.

In the driver's `rules.mk`, the `MODULE_TYPE` should
be `driver`. This will install the driver shared lib in `/boot/driver/`.

If your driver is built outside Zircon, install them in `/system/driver/`
. The Device Coordinator looks in those directories for loadable
drivers.

## Declaring a driver

At a minimum, a driver should contain the driver declaration and implement the
`bind()` driver op.

Drivers are loaded and bound to a device when the Device Coordinator
successfully finds a matching driver for a device. A driver declares the
devices it is compatible with via bindings.
The following bind program
declares the [AHCI driver](../../system/dev/block/ahci/ahci.c):

```
ZIRCON_DRIVER_BEGIN(ahci, ahci_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x01),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x06),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x01),
ZIRCON_DRIVER_END(ahci)
```

The AHCI driver has 4 directives in the bind program. `"zircon"` is the vendor
id and `"0.1"` is the driver version. It binds with `ZX_PROTOCOL_PCI` devices
with PCI class 1, subclass 6, interface 1.

The [PCI driver](../../system/dev/bus/pci/kpci.c) publishes the matching
device with the following properties:

```
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

Binding variables and macros are defined in
[zircon/driver/binding.h](../../system/public/zircon/driver/binding.h).
If you are introducing a new device class, you may need to introduce new
binding variables in that file.
Binding variables are 32-bit values. If your
variable value requires greater than a 32-bit value,
split them into multiple 32-bit variables. An
example is ACPI HID values, which are 8 characters (64-bits) long.
It is split into `BIND_ACPI_HID_0_3` and `BIND_ACPI_HID_4_7`.

Binding directives are evaluated sequentially. The branching directives
`BI_GOTO()` and `BI_GOTO_IF()` allow you to jump forward to the matching
label, defined by `BI_LABEL()`.

`BI_ABORT_IF_AUTOBIND` may be used (usually as the first instruction)
to prevent the default automatic binding behaviour.
In that case, a driver can be bound to a device using
`ioctl_device_bind()` call


## Driver binding

A driver’s `bind()` function is called when it is matched to a device.
Generally a driver will initialize any data structures needed for the device
and initialize hardware in this function. It should not perform any
time-consuming tasks or block in this function, because it is invoked from the
devhost's RPC thread and it will not be able to service other requests in the
meantime. Instead, it should spawn a new thread to perform lengthy tasks.

The driver should make no assumptions about the state of the hardware in
`bind()`, resetting the hardware or otherwise ensuring it is in a known state.
Because the system recovers from a
driver crash by re-spawning the devhost, the hardware may be in an unknown
state when `bind()` is invoked.

A driver is required to publish a `zx_device_t` in `bind()` by calling
`device_add()`. This is necessary for the Device Coordinator to keep
track of the
device lifecycle. If the driver is not able to publish a functional device in
`bind()`, for example if it is initializing the full device in a thread, it
should publish an invisible device, and make this device visible when
initialization is completed. See `DEVICE_ADD_INVISIBLE` and
`device_make_visible()` in
[zircon/ddk/driver.h](../../system/ulib/ddk/include/ddk/driver.h).

There are generally four outcomes from `bind()`:

1. The driver determines the device is supported and does not need to do any
heavy lifting, so publishes a new device via `device_add()` and returns
`ZX_OK`.

2. The driver determines that even though the bind program matched, the device
cannot be supported (maybe due to checking hw version bits or whatnot) and
returns an error.

3. The driver needs to do further initialization before the device is ready or
it’s sure it can support it, so it publishes an invisible device and kicks off
a thread to keep working, while returning `ZX_OK`. That thread will eventually
make the device visible or, if it cannot successfully initialize it, remove it.

4. The driver represents a bus or controller with 0..n children which may
dynamically appear or disappear. In this case it should publish a device
immediately representing the bus or controller, and then dynamically publish
children (that downstream drivers will bind to) representing hardware on that
bus. Examples: AHCI/SATA, USB, etc.

After a device is added and made visible by the system, it is made available
to client processes and for binding by compatible drivers.

## Device protocols

A driver provides a set of device ops and optional protocol ops to a device.
Device ops implement the device lifecycle methods and the external interface
to the device that are called by other user space applications and services.
Protocol ops implement the ddk-internal protocols of the device that are
called by other drivers.

You can pass one set of protocol ops for the device in `device_add_args_t`. If
a device supports multiple protocols, implement the `get_protocol()` device
op. A device can only have one protocol id. The protocol id corresponds to the
class the device is published under in devfs.

Device protocol headers are found in
[ddk/protocol/](../../system/ulib/ddk/include/ddk/protocol). Ops and any data
structures passed between drivers should be defined in this header.

## Driver operation

A driver generally operates by servicing client requests from children drivers
or other processes. It fulfills those requests either by communicating
directly with hardware (for example, via MMIO) or by communicating with its
parent device (for example, queuing a USB transaction).

External client requests from processes outside the devhost are fulfilled by
the device ops `read()`, `write()`, and `ioctl()`. Requests from children
drivers, generally in the same process, are fulfilled by device
protocols corresponding to the device class. Driver-to-driver requests should
use device protocols instead of device ops.

A device can get a protocol supported by its parent by calling
`device_get_protocol()` on its parent device.

## Device interrupts

Device interrupts are implemented by interrupt objects, which are a type of
kernel objects. A driver requests a handle to the device interrupt from its
parent device in a device protocol method. The handle returned will be bound
to the appropriate interrupt for the device, as defined by a parent driver.
For example, the PCI protocol implements `map_interrupt()` for PCI children. A
driver should spawn a thread to wait on the interrupt handle.

The kernel will automatically handle masking and unmasking the
interrupt as appropriate, depending on whether the interrupt is edge-triggered
or level-triggered. For level-triggered hardware interrupts,
[zx_interrupt_wait()](../syscalls/interrupt_wait.md) will mask the interrupt
before returning and unmask the interrupt when it is called again the next
time. For edge-triggered interrupts, the interrupt remains unmasked.

The interrupt thread should not perform any long-running tasks. For drivers
that perform lengthy tasks, use a worker thread.

You can signal an interrupt handle with
[zx_interrupt_signal()](../syscalls/interrupt_signal.md) on slot
**ZX_INTERRUPT_SLOT_USER** to return from `zx_interrupt_wait()`. This is
necessary to shut down the interrupt thread during driver clean up.

## Ioctl

Ioctls for each device class are defined in
[zircon/device/](../../system/public/zircon/device). Ioctls may accept or
return handles. The `IOCTL_KIND_*` defines in
[zircon/device/ioctl.h](../../system/public/zircon/device/ioctl.h), used in
the ioctl declaration, defines whether the ioctl accepts or returns handles
and how many. The driver owns the handles passed in and should close the
handles when they’re no longer needed, unless it returns
`ZX_ERR_NOT_SUPPORTED` in which case the devhost RPC layer will close the
handles.

## Protocol ops vs. ioctls

Protocol ops define the DDK-internal API for a device. Ioctls define the
external API. Define a protocol op if the function is primarily meant to be
called by other drivers, and generally a driver should call a protocol op on
its parent instead of an ioctl.

## Isolate devices

Devices that are added with `DEVICE_ADD_MUST_ISOLATE` spawn a new devhost. The
device exists in both the parent devhost and as the root of the new devhost.
The driver is provided a channel in `create()` when it creates the proxy
device, or the “bottom half” that runs in the new devhost. The proxy device
should cache this channel for when it needs to communicate with the top half,
for example if it needs to call API on the parent device.

`rxrpc()` is invoked on the top half when this channel is written to by the
bottom half. There is no common wire protocol for this channel. For an
example, refer to the [PCI driver](../../system/dev/bus/pci).

NOTE: This is a mechanism used by various bus devices and not something
general drivers should have to worry about. (please ping swetland if you think
you need to use this)

## Logging

[ddk/debug.h](../../system/ulib/ddk/include/ddk/debug.h) defines the
`zxlogf(<log_level>,...)` macro. The log messages are printed to the system
debuglog over the network and on the serial port if available for the device.
By default, `ERROR` and `INFO` are always printed. You can control the log
level for a driver by passing the boot cmdline
`driver.<driver_name>.log=+<level>,-<level>`. For example,
`driver.sdhci.log=-info,+trace,+spew` enables the `TRACE` and `SPEW` logs and
disable the `INFO` logs for the sdhci driver.

The log levels prefixed by "L" (`LERROR`, `LINFO`, etc.) do not get sent over
the network and is useful for network logging.

## Driver testing

`ZX_PROTOCOL_TEST` provides a mechanism to test drivers by running the driver
under test in an emulated environment. Write a driver that binds to a
`ZX_PROTOCOL_TEST` device. This driver should publish a device that the driver
under test can bind to, and it should implement the protocol functions the
driver under test invokes in normal operation. This helper driver should be
declared with `BI_ABORT_IF_AUTOBIND` in the bindings.

The test harness calls `ioctl_test_create_device()` on `/dev/misc/test`, which
will create a `ZX_PROTOCOL_TEST` device and return its path. Then it calls
`ioctl_device_bind()` with the helper driver on the newly created device.
This approach generally works better for mid-layer protocol drivers. It's
possible to emulate real hardware with the same approach but it may not be as
useful.

The functions defined in
[ddk/protocol/test.h](../../system/ulib/ddk/include/ddk/protocol/test.h) are
for testing libraries that run as part of a driver. For an example, refer to
[system/ulib/ddk/test](../../system/ulib/ddk/test). The test harness for these
tests is
[system/utest/driver-tests/main.c](../../system/utest/driver-tests/main.c)

## Driver rights

Although drivers run in user space processes, they have a more restricted set
of rights than normal processes. Drivers are not allowed to access the
filesystem, including devfs. That means a driver cannot interact with
arbitrary devices. If your driver needs to do this, consider writing a service
instead. For example, the virtual console is implemented by the
[virtcon](../../system/core/virtcon) service.

Privileged operations such as `zx_vmo_create_contiguous()` and
[zx_interrupt_create](../syscalls/interrupt_create.md) require a root resource
handle. This handle is not available to drivers other than the system driver
([ACPI](../../system/dev/bus/acpi) on x86 systems and
[platform](../../system/dev/bus/platform) on ARM systems). A device should
request its parent to perform such operations for it. Contact the author
of the parent driver if its protocol does not address this use case.

Similarly, a driver is not allowed to request arbitrary MMIO ranges,
interrupts or GPIOs. Bus drivers such as PCI and platform only return the
resources associated to the child device.
