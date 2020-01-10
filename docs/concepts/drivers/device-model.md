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

Note: The current policy is simple (each device representing a physical bus-master
capable hardware device and its children are placed into a separate devhost).  It
will evolve to provide finer-grained partitioning.


## Devices, Drivers, and Device Hosts

Here's a (slightly trimmed for clarity) dump of the tree of devices in
Zircon running on Qemu x86-64:

```sh
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

Devices may implement Protocols, which are Banjo ABIs used by child devices
to interact with parent devices in a device-specific manner. The
[PCI Protocol](/zircon/system/banjo/ddk.protocol.pci/pci.banjo),
[USB Protocol](/zircon/system/banjo/ddk.protocol.usb/usb.banjo),
[Block Core Protocol](/zircon/system/banjo/ddk.protocol.block/block.banjo), and
[Ethernet Protocol](/zircon/system/banjo/ddk.protocol.ethernet/ethernet.banjo), are
examples of these.  Protocols are usually in-process interactions between
devices in the same devhost, but in cases of driver isolation, they may take
place via RPC to a "higher" devhost (via proxy).

Devices may implement Interfaces, which are
[FIDL](/docs/development/languages/fidl/README.md) RPC protocols
that clients (services, applications, etc) use.  The base device interface
supports POSIX style open/close/read/write IO.  Interfaces are supported via
the `message()` operation in the base device interface.

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
within class directories are unique but not meaningful, and are assigned on demand.

Note: Currently names in class directories are 3 digit decimal numbers, but they
are likely to change form in the future.  Clients should not assume there is any
specific meaning to a class alias name.


## Device Driver Lifecycle

Device drivers are loaded into devhost processes when it is determined they are
needed.  What determines if they are loaded or not is the Binding Program, which
is a description of what device a driver can bind to.  The Binding Program is
defined using a small domain specific language, which is compiled to bytecode that
is distributed with the driver.


An example Binding Program from the Intel Ethernet driver:

```
fuchsia.device.protocol == fuchsia.pci.protocol.PCI_DEVICE;
fuchsia.pci.vendor == fuchsia.pci.vendor.INTEL;
accept fuchsia.pci.device {
    0x100E, // Qemu
    0x15A3, // Broadwell
    0x1570, // Skylake
    0x1533, // I210 standalone
    0x15b7, // Skull Canyon NUC
    0x15b8, // I219
    0x15d8, // Kaby Lake NUC
}
```

The bind compiler takes a binding program and outputs a C header file that
defines a macro, `ZIRCON_DRIVER`. The `ZIRCON_DRIVER` macro includes the
necessary compiler directives to put the binding program into an ELF NOTE
section, allowing it to be inspected by the Device Coordinator without needing
to fully load the driver into its process.

The second parameter to `ZIRCON_DRIVER` is a `zx_driver_ops_t` structure pointer
(defined by [`ddk/driver.h`](/zircon/system/ulib/ddk/include/ddk/driver.h) which
defines the init, bind, create, and release methods.

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
various structures and functions are defined in [`device.h`](/zircon/system/ulib/ddk/include/ddk/device.h)

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

Devices are reference counted. A reference is acquired when a driver creates
the device with `device_add()` and when the device is opened by a remote process
via the Device Filesystem.

From the moment that `device_add()` is called without the **DEVICE_ADD_INVISIBLE**
flag, or `device_make_visible()` is called on an invisible device, other device
ops may be called by the Device Host.

When `device_async_remove()` is called on a device, this schedules the removal
of the device and its descendents.

The removal of a device consists of four parts: running the device's `unbind()` hook,
removal of the device from the Device Filesystem, dropping the reference acquired
by `device_add()` and running the device's `release()` hook.

When the `unbind()` method is invoked, this signals to the driver it should start
shutting the device down, and call `device_unbind_reply()` once it has finished unbinding.
This is an optional hook. If it is not implemented, it is treated as `device_unbind_reply()`
was called immediately.

Since a child device may have work in progress when its `unbind()` method is
called, it's possible that the parent device (which already completed
unbinding) could continue to receive device method calls or protocol method
calls on behalf of that child.  It is advisable that before completing unbinding,
the parent device should arrange for these methods to return errors, so that
calls from a child before the child removal is completed do not start more
work or cause unexpected interactions.

The `release()` method is only called after the creating driver has completed
unbinding, all open instances of that device have been closed,
and all children of that device have been unbound and released.  This
is the last opportunity for the driver to destroy or free any resources associated
with the device.  It is not valid to refer to the `zx_device_t` for that device
after `release()` returns.  Calling any device methods or protocol methods for
protocols obtained from the parent device past this point is illegal and will
likely result in a crash.

### An Example of the Tear-Down Sequence

To explain how the `unbind()` and `release()` work during the tear-down process,
below is an example of how a USB WLAN driver would usually handle it.  In short,
the `unbind()` call sequence is top-down while the `release()` sequence is bottom-up.

Note that this is just an example. This might not match what exactly the real WLAN driver
is doing.

Assume a WLAN device is plugged in as a USB device, and a PHY interface has been
created under the USB device. In addition to the PHY interface, 2 MAC interfaces
have been created under the PHY interface.

```
            +------------+
            | USB Device | .unbind()
            +------------+ .release()
                  |
            +------------+
            |  WLAN PHY  | .unbind()
            +------------+ .release()
              |        |
    +------------+  +------------+
    | WLAN MAC 0 |  | WLAN MAC 1 | .unbind()
    +------------+  +------------+ .release()
```

Now, we unplug this USB WLAN device.

* The USB XHCI detects the removal and calls `device_async_remove(usb_device)`.

* This will lead to the USB device's `unbind()` being called.
  Once it completes unbinding, it would call `device_unbind_reply()`.

```c
    usb_device_unbind(void* ctx) {
        // Stop interrupt or anything to prevent incoming requests.
        ...

        device_unbind_reply(usb_dev);
    }
```

* When the USB device completes unbinding, the WLAN PHY's `unbind()` is called.
  Once it completes unbinding, it would call `device_unbind_reply()`.

```c
    wlan_phy_unbind(void* ctx) {
        // Stop interrupt or anything to prevent incoming requests.
        ...

        device_unbind_reply(wlan_phy);
    }
```

* When wlan_phy completes unbinding, unbind() will be called on all of its children
  (wlan_mac_0, wlan_mac_1).

```c
    wlan_mac_unbind(void* ctx) {
        // Stop accepting new requests, and notify clients that this device is offline (often just
        // by returning an ZX_ERR_IO_NOT_PRESENT to any requests that happen after unbind).
        ...

        device_unbind_reply(iface_mac_X);
    }
```

* Once all the clients of a device have been removed, and that device has no children,
  its refcount will reach zero and its release() method will be called.

* WLAN MAC 0 and 1's `release()` are called.

```c
    wlan_mac_release(void* ctx) {
        // Release sources allocated at creation.
        ...

        // Delete the object here.
        ...
    }
```

* The wlan_phy has no open connections, but still has child devices (wlan_mac_0 and wlan_mac_1).
  Once they have both been released, its refcount finally reaches zero and its release()
  method is invoked.

```c
    wlan_phy_release(void* ctx) {
        // Release sources allocated at creation.
        ...

        // Delete the object here.
        ...
    }
```

* Once the USB device now has no child devices or open connections, its `release()` would be called.
