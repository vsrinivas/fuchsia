

<!--
    (C) Copyright 2018 The Fuchsia Authors. All rights reserved.
    Use of this source code is governed by a BSD-style license that can be
    found in the LICENSE file.
-->

# Getting Started

This document is part of the [Driver Development Kit tutorial](ddk-tutorial.md) documentation.

Writing a device driver is often viewed as a daunting task, fraught with complexities
and requiring arcane knowledge of little-known kernel secrets.

The goal of this section is to demystify the process; you'll learn everything you
need to know about how to write device drivers, starting with what they do, how
they work, and how they fit into the overall system.

## Overview

At the highest level, a device driver's job is to provide a uniform interface to
a particular device, while hiding details specific to the device's implementation.

Two different ethernet drivers, for example, both allow a client to send packets
out an interface, using the exact same C language function.
Each driver is responsible for managing its own hardware in a way that makes the
client interfaces identical, even though the hardware is different.

Note that the interfaces that are provided by the driver may be "intermediate" &mdash;
that is, they might not necessarily represent the "final" device in the chain.

Consider a PCI-based ethernet device.
First, a base PCI driver is required that understands how to talk to the PCI bus itself.
This driver doesn't know anything about ethernet, but it does know
how to deal with the specific PCI chipset present on the machine.

It enumerates the devices on that bus, collects information
from the various registers on each device, and provides functions that allow
its clients (such as the PCI-based ethernet driver) to perform PCI operations
like allocating an interrupt or a DMA channel.

Thus, this base PCI driver provides services to the ethernet driver, allowing
the ethernet driver to manage its associated hardware.

At the same time, other devices (such as a video card) could also use the base PCI
driver in a similar manner to manage their hardware.

## The Zircon model

In order to provide maximum flexibility, drivers in the Zircon world are allowed
to bind to matching "parent" devices, and publish "children" of their own.
This hierarchy extends as required: one driver might publish a child, only to have
another driver consider that child their parent, with the second driver publishing
its own children, and so on.

In order to understand how this works, let's follow the PCI-based ethernet example.

The system starts by providing a special "PCI root" parent.
Effectively, it's saying "I know that there's a PCI bus on this system, when you
find it, bind it *here*."

> The "Advanced Topics" section below has more details about this process.

Drivers are evaluated by the system (a directory is searched), and drivers that
match are automatically bound.

In this case, a driver that binds to a "PCI root" parent is found, and bound.

This is the base PCI driver.
It's job is to configure the PCI bus, and enumerate the peripherals on the bus.

The PCI bus has specific conventions for how peripherals are identified:
a combination of a Vendor ID (**VID**) and Device ID (**DID**) uniquely identifies
all possible PCI devices.
During enumeration, these values are read from the peripheral, and new parent
nodes are published containing the detected VID and DID (and a host of other
information).

Every time a new device is published, the same process as described above (for
the initial PCI root device publication) repeats;
that is, drivers are evaluated by the system, searching for drivers that match
up with the new parents' characteristics.

Whereas with the PCI root device we were searching for a driver that matched
a certain kind of functionality (called a "protocol," we'll see this shortly), in
this case, however, we're searching for drivers that match a different
protocol, namely one that satisfies the requirements of "is a PCI device and
has a given VID and DID."

If a suitable driver is found (one that matches the required protocol, VID and
DID), it's bound to the parent.

As part of binding, we initialize the driver &mdash; this involves such operations
as setting up the card for operation, bringing up the interface(s), and
publishing a child or children of this device.
In the case of the PCI ethernet driver, it publishes the "ethernet" interface,
which conforms to yet another protocol, called the "ethernet implementation" protocol.
This protocol represents a common protocol that's close to the functions that
clients use (but is one step removed; we'll come back to this).

### Protocols

We mentioned three protocols above:

*   the PCI root protocol (`ZX_PROTOCOL_PCIROOT`),
*   the PCI device protocol (`ZX_PROTOCOL_PCI`), and
*   the ethernet implementation protocol (`ZX_PROTOCOL_ETHERNET_IMPL`).

The names in brackets are the C language constants corresponding to the protocols, for reference.

So what is a protocol?

A protocol is a strict interface definition.

The ethernet driver published an interface that conforms to `ZX_PROTOCOL_ETHERNET_IMPL`.
This means that it must provide a set of functions defined in a data structure
(in this case, `ethmac_protocol_ops_t`).

These functions are common to all devices implementing the protocol &mdash; for example,
all ethernet devices must provide a function that queries the MAC address of the
interface.

Other protocols will of course have different requirements for the functions they
must provide.
For example a block device will publish an interface that conforms to the
"block implementation protocol" (`ZX_PROTOCOL_BLOCK_IMPL`) and
provide functions defined by `block_protocol_ops_t`.
This protocol includes a function that returns the size of the device in blocks,
for example.

We'll examine these protocols in the following chapters.

# Advanced Topics

The above has presented a big picture view of Zircon drivers, with a focus on protocols.

In this section, we'll examine some advanced topics, such as platform dependent
and platform independent code decoupling,
the "miscellaneous" protocol, and how protocols and processes are mapped.

## Platform dependent vs platform independent

Above, we mentioned that `ZX_PROTOCOL_ETHERNET_IMPL` was "close to" the functions used
by the client, but one step removed.
That's because there's one more protocol, `ZX_PROTOCOL_ETHERNET`, that sits between
the client and the driver.
This additional protocol is in place to handle functionality common to all ethernet
drivers (in order to avoid code duplication).
Such functionality includes buffer management, status reporting, and administrative
functions.

This is effectively a "platform dependent" vs "platform independent" decoupling;
common code exists in the platform independent part (once), and driver-specific code
is implemented in the platform dependent part.

This architecture is repeated in multiple places.
With block devices, for example, the hardware driver binds to the bus (e.g., PCI)
and provides a `ZX_PROTOCOL_BLOCK_IMPL` protocol.
The platform independent driver binds to `ZX_PROTOCOL_BLOCK_IMPL`, and publishes the
client-facing protocol, `ZX_PROTOCOL_BLOCK`.

You'll also see this with the display controllers, I<sup>2</sup>C bus, and serial drivers.

## Miscellaneous protocol

In [simple drivers](simple.md), we show the code for several drivers that illustrate
basic functionality, but don't provide services related to a specific protocol
(i.e., they are not "ethernet" or "block" devices).
These drivers are bound to `ZX_PROTOCOL_MISC_PARENT`.

> @@@ More content?

## Process / protocol mapping

In order to keep the discussions above simple, we didn't talk about process separation
as it relates to the drivers.
To understand the issues, let's see how other operating systems deal with them,
and compare that to the Zircon approach.

In a monolithic kernel, such as Linux, many drivers are implemented within the kernel.
This means that they share the same address space, and effectively live in the same
"process."

The major problem with this approach is fault isolation / exploitation.
A bad driver can take out the entire kernel, because it lives in the same address
space and thus has privileged access to all kernel memory and resources.
A compromised driver can present a security threat for the same reason.

The other extreme, that is, putting each and every driver service into its own
process, is used by some microkernel operating systems.
Its major drawback is that if one driver relies on the services of another driver,
the kernel must effect at least a context switch operation (if not a data transfer
as well) between the two driver processes.
While microkernel operating systems are usually designed to be fast at these
kinds of operations, performing them at high frequency is undesirable.

The approach taken by Zircon is based on the concept of a device host (**devhost**).
A devhost is a process that contains a protocol stack &mdash; that is, one or
more protocols that work together.
The devhost loads drivers from ELF shared libraries (called Dynamic Shared Objects,
or **DSO**s).
In the [simple drivers](simple.md) section, we'll see the meta information
that's contained in the DSO to facilitate the discovery process.

The protocol stack effectively allows the creation of a complete "driver" for
a device, consisting of platform dependent and platform independent components,
in a self-contained process container.

For the advanced reader, take a look at the `dm dump` command available from
the Zircon command line.
It displays a tree of devices, and shows you the process ID, DSO name, and
other useful information.

Here's a highly-edited version showing just the PCI ethernet driver parts:

```
1. [root]
2.    [sys]
3.       <sys> pid=1416 /boot/driver/bus-acpi.so
4.          [acpi] pid=1416 /boot/driver/bus-acpi.so
5.          [pci] pid=1416 /boot/driver/bus-acpi.so
            ...
6.             [00:02:00] pid=1416 /boot/driver/bus-pci.so
7.                <00:02:00> pid=2052 /boot/driver/bus-pci.proxy.so
8.                   [intel-ethernet] pid=2052 /boot/driver/intel-ethernet.so
9.                      [ethernet] pid=2052 /boot/driver/ethernet.so
```

From the above, you can see that process ID `1416` (lines 3 through 6)
is the Advanced Configuration and Power Interface (**ACPI**) driver, implemented
by the DSO `bus-acpi.so`.

During primary enumeration, the ACPI DSO detected a PCI bus.
This caused the publication of a parent with `ZX_PROTOCOL_PCI_ROOT` (line 5,
causing the appearance of the `[pci]` entry),
which then caused the devhost to load the `bus-pci.so` DSO and bind to it.
That DSO is the "base PCI driver" to which we've been referring throughout the
discussions above.

During its binding, the base PCI driver enumerated the PCI bus, and found an ethernet
card (line 6 detects bus 0, device 2, function 0, shown as `[00:02:00]`).
(Of course, many other devices were found as well, but we've removed them from
the above listing for simplicity).

The detection of this device then caused the base PCI driver to publish a new parent
with `ZX_PROTOCOL_PCI` and the device's VID and DID.
Additionally, a new devhost (process ID `2052`) was created and loaded with the
`bus-pci.proxy.so` DSO (line 7).
This proxy serves as the interface from the new devhost (pid `2052`) to the base PCI
driver (pid `1416`).

> This is where the decision was made to "sever" the device driver into its own
> process &mdash; the new devhost and the base PCI driver now live in two
> different processes.

The new devhost `2052` then finds a matching child (the `intel-ethernet.so`
DSO on line 8; it's considered a match because it has `ZX_PROTOCOL_PCI` and the correct
VID and DID).
That DSO publishes a `ZX_PROTOCOL_ETHERNET_IMPL`, which binds to a matching
child (the `ethernet.so` DSO on line 9; it's considered a match because it has a
`ZX_PROTOCOL_ETHERNET_IMPL` protocol).

What's not shown by this chain is that the final DSO (`ethernet.so`) publishes
a `ZX_PROTOCOL_ETHERNET` &mdash; that's the piece that clients can use, so of
course there's no further "device" binding involved.


