# Zircon Device Model

## Introduction

In Zircon, device drivers are implemented as ELF shared libraries (DSOs) which are
loaded into Device Host (devhost) processes.  The Device Manager (devmgr) process,
contains the Device Coordinator which keeps track of drivers and devices, manages
the discovery of drivers, the creation and direction of Device Host processes, and
maintains the Device Filesystem (devfs), which is the mechanism through which userspace
services and applications (constrained by their namespaces) gain access to devices.

The Device Coordinator views devices as part of a single unified tree.
The branches (and sub-branches) of that tree consist of some number of
devices within a Device Host process.  The decision as to how to sub-divide
the overall tree among Device Hosts is based on system policy for isolating
drivers for security or stability reasons and colocating drivers for performance
reasons.

NOTE: The current policy is simple (each device representing a physical bus-master
capable hardware device and its children are place into a separate devhost).  It
will evolve to provide finer-grained partitioning.


## Devices, Drivers, and Device Hosts

Here's a (slightly trimmed for clarity) dump of the tree of devices in
Zircon running on Qemu x86-64:

```
$ dm dump
[root]
   <root> pid=1509
      [null] pid=1509 /boot/driver/builtin.so
      [zero] pid=1509 /boot/driver/builtin.so
   [misc]
      <misc> pid=1645
         [console] pid=1645 /boot/driver/console.so
         [dmctl] pid=1645 /boot/driver/dmctl.so
         [ptmx] pid=1645 /boot/driver/pty.so
         [i8042-keyboard] pid=1645 /boot/driver/pc-ps2.so
            [hid-device-001] pid=1645 /boot/driver/hid.so
         [i8042-mouse] pid=1645 /boot/driver/pc-ps2.so
            [hid-device-002] pid=1645 /boot/driver/hid.so
   [sys]
      <sys> pid=1416 /boot/driver/bus-acpi.so
         [acpi] pid=1416 /boot/driver/bus-acpi.so
         [pci] pid=1416 /boot/driver/bus-acpi.so
            [00:00:00] pid=1416 /boot/driver/bus-pci.so
            [00:01:00] pid=1416 /boot/driver/bus-pci.so
               <00:01:00> pid=2015 /boot/driver/bus-pci.proxy.so
                  [bochs_vbe] pid=2015 /boot/driver/bochs-vbe.so
                     [framebuffer] pid=2015 /boot/driver/framebuffer.so
            [00:02:00] pid=1416 /boot/driver/bus-pci.so
               <00:02:00> pid=2052 /boot/driver/bus-pci.proxy.so
                  [intel-ethernet] pid=2052 /boot/driver/intel-ethernet.so
                     [ethernet] pid=2052 /boot/driver/ethernet.so
            [00:1f:00] pid=1416 /boot/driver/bus-pci.so
            [00:1f:02] pid=1416 /boot/driver/bus-pci.so
               <00:1f:02> pid=2156 /boot/driver/bus-pci.proxy.so
                  [ahci] pid=2156 /boot/driver/ahci.so
            [00:1f:03] pid=1416 /boot/driver/bus-pci.so
```

The names in square brackets are devices.  The names in angle brackets are
proxy devices, which are instantiated in the "lower" devhost, when process
isolation is being provided.  The pid= field indicates the process object
id of the devhost process that device is contained within.  The path indicates
which driver implements that device.

Above, for example, the pid 1416 devhost contains the pci bus driver, which has
created devices for each PCI device in the system.  PCI device 00:02:00 happens
to be an intel ethernet interface, which we have a driver for (intel-ethernet.so).
A new devhost (pid 2052) is created, set up with a proxy device for PCI 00:02:00,
and the intel ethernet driver is loaded and bound to it.

Proxy devices are invisible within the Device filesystem, so this ethernet device
appears as `/dev/sys/pci/00:02:00/intel-ethernet`.


## Protocols, Interfaces, and Classes

Devices may implement Protocols, which are C ABIs used by child devices
to interact with parent devices in a device-specific manner. The
[PCI Protocol](../../system/ulib/ddk/include/ddk/protocol/pci.h),
[USB Protocol](../../system/ulib/ddk/include/ddk/protocol/usb.h),
[Block Core Protocol](../../system/ulib/ddk/include/ddk/protocol/block.h), and
[Ethermac Protocol](../../system/ulib/ddk/include/ddk/protocol/ethernet.h), are
examples of these.  Protocols are usually in-process interactions between
devices in the same devhost, but in cases of driver isolation, they may take
place via RPC to a "higher" devhost.

Devices may implement Interfaces, which are RPC protocols that clients (services,
applications, etc).  The base device interface supports posix style open/close/read/write
style IO.  Currently Interfaces are supported via the ioctl operation in the base
device interface.  In the future, Fuchsia's interface definition language and bindings
(FIDL) will be supported.

In many cases a Protocol is used to allow drivers to be simpler by taking advantage
of a common implementation of an Interface.  For example, the "block" driver implements
the common block interface, and binds to devices implementing the Block Core Protocol,
and the "ethernet" driver does the same thing for the Ethernet Interface and Ethermac
Protocol.  Some protocols, such as the two cited here, make use of shared memory, and
non-rpc signaling for more efficient, lower latency, and higher throughput than could
be achieved otherwise.

Classes represent a promise that a device implements an Interface or Protocol.
Devices exist in the Device Filesystem under a topological path, like
`/sys/pci/00:02:00/intel-ethernet`.  If they are a specific class, they also appear
as an alias under `/dev/class/CLASSNAME/...`.  The `intel-ethernet` driver implements
the Ethermac interface, so it also shows up at `/dev/class/ethermac/000`.  The names
within class directories are unique but not meaningful, assigned on demand.

NOTE: Currently names in class directories are 3 digit decimal numbers, but they
are likely to change form in the future.  Clients should not assume there is any
specific meaning to a class alias name.


## Device Driver Lifecycle

Device drivers are loaded into devhost processes when it is determined they are
needed.  What determines if they are loaded or not is the Binding Program, which
is a description of what device a driver can bind to.  The Binding Program is
defined using macros in [`ddk/binding.h`](../../system/ulib/ddk/include/ddk/binding.h)

An example Binding Program from the Intel Ethernet driver:
```
ZIRCON_DRIVER_BEGIN(intel_ethernet, intel_ethernet_driver_ops, "zircon", "0.1", 9)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x100E), // Qemu
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15A3), // Broadwell
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1570), // Skylake
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1533), // I210 standalone
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15b7), // Skull Canyon NUC
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15b8), // I219
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x15d8), // Kaby Lake NUC
ZIRCON_DRIVER_END(intel_ethernet)
```

The ZIRCON_DRIVER_BEGIN and _END macros include the necessary compiler directives
to put the binding program into an ELF NOTE section which allows it to be inspected
by the Device Coordinator without needing to fully load the driver into its process.
The second parameter to the _BEGIN macro is a zx_driver_ops_t structure pointer (defined
by `[ddk/driver.h](../../system/ulib/ddk/include/ddk/driver.h)` which defines the
init, bind, create, and release methods.

`init()` is invoked when a driver is loaded into a Device Host process and allows for
any global initialization.  Typically none is required.  If the `init()` method is
implemented and fails, the driver load will fail.

`bind()` is invoked to offer the driver a device to bind to.  The device is one that
has matched the bind program the driver has published.  If the `bind()` method succeeds,
the driver **must** create a new device and add it as a child of the device passed in
to the `bind()` method.  See Device Lifecycle for more information.

`create()` is invoked for platform/system bus drivers or proxy drivers.  For the
vast majority of drivers, this method is not required.

`release()` is invoked before the driver is unloaded, after all devices it may have
created in `bind()` and elsewhere have been destroyed.  Currently this method is
**never** invoked.  Drivers, once loaded, remain loaded for the life of a Device Host
process.


## Device Lifecycle

Within a Device Host process, devices exist as a tree of `zx_device_t` structures
which are opaque to the driver.  These are created with `device_add()` which the
driver provides a `zx_protocol_device_t` structure to.  The methods defined by the
function pointers in this structure are the "[device ops](device-ops.md)".  The
various structures and functions are defined in [`device.h`](../../system/ulib/ddk/include/ddk/device.h)

The `device_add()` function creates a new device, adding it as a child to the
provided parent device.  That parent device **must** be either the device passed
in to the `bind()` method of a device driver, or another device which has been
created by the same device driver.

A side-effect of `device_add()` is that the newly created device will be added
to the global Device Filesystem maintained by the Device Coordinator.  If the
device is created with the **DEVICE_ADD_INVISIBLE** flag, it will not be accessible
via opening its node in devfs until `device_make_visible()` is invoked.  This
is useful for drivers that have to do extended initialization or probing and
do not want to visibly publish their device(s) until that succeeds (and quietly
remove them if that fails).

Devices are reference counted.  When a driver creates one with `device_add()`,
it then holds a reference on that device until it eventually calls `device_remove()`.
If a device is opened by a remote process via the Device Filesystem, a reference
is acquired there as well.  When a device's parent is removed, its `unbind()`
method is invoked.  This signals to the driver that it should start shutting
the device down and remove and child devices it has created by calling `device_remove()`
on them.

Since a child device may have work in progress when its `unbind()` method is
called, it's possible that the parent device which just called `device_remove()`
on the child could continue to receive device method calls or protocol method
calls on behalf of that child.  It is advisable that before removing its children,
the parent device should arrange for these methods to return errors, so that
calls from a child before the child removal is completed do not start more
work or cause unexpected interactions.

From the moment that `device_add()` is called without the **DEVICE_ADD_INVSIBLE**
flag, or `device_make_visible()` is called on an invisible device, other device
ops may be called by the Device Host.

The `release()` method is only called after the creating driver has called
`device_remove()` on the device, all open instances of that device have been
closed, and all children of that device have been removed and released.  This
is the last opportunity for the driver to destroy or free any resources associated
with the device.  It is not valid to refer to the `zx_device_t` for that device
after `release()` returns.  Calling any device methods or protocol methods for
protocols obtained from the parent device past this point is illegal and will
likely result in a crash.
