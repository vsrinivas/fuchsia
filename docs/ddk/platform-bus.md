# Platform Bus

## Introduction

The platform bus manages drivers for devices on SOC-based platforms.
The platform bus is a driver itself (located at
[system/dev/bus/platform](../../system/dev/bus/platform))
and is started automatically by the device manager on platforms that need it.
Currently the platform bus is used on all arm64 platforms.
It is not used on x86 platforms, since ACPI performs a similar role on x86.
On platforms that use it, the platform bus is represented in the device tree as `/sys`.

The platform bus serves the following purposes:

* Creating devices for binding the drivers needed for a particular platform.

* Abstracting away the platform specific details for drivers, such as MMIO addresses,
  IRQ and GPIO numbers, etc. so a single driver for a particular hardware device or IP
  can be used across multiple platforms.

* Sandboxing, so drivers can access the MMIO ranges, IRQs, GPIOs, I2C addresses, etc.
  that they need, but nothing else.

## Initialization

The platform bus driver is a generic driver that contains no specific information about the
platform it is running on.
To provide the platform specific logic for the platform bus, it loads a platform specific
helper driver called the "board driver".

When Zircon boots, the `vid`, `pid` and `board_name` are read from the `BOOTDATA_PLATFORM_ID`
section in the bootdata. This is passed to the Platform Bus driver via the device argument list.
The Platform Bus then adds a device with protocol `ZX_PROTOCOL_PLATFORM_BUS` with the
`BIND_PLATFORM_DEV_VID` and `BIND_PLATFORM_DEV_PID` binding variables set to the vid and did
from the bootdata information. The board driver then binds to this device.

The board driver uses the `ZX_PROTOCOL_PLATFORM_BUS` protocol
(see [system/ulib/ddk/include/ddk/protocol/platform-bus.h](../../system/ulib/ddk/include/ddk/protocol/platform-bus.h))
to communicate with the platform bus driver.
After the board driver initializes itself, it calls `pbus_set_interface()` to register itself
with the platform bus.
This tells the platform bus that the board driver is ready to go, and provides an interface
the platform bus driver can use to communicate with the board driver.

## Platform Devices

After the board driver calls `pbus_set_interface()` to register itself,
it will then typically call `pbus_device_add()` one or more times to create devices
for platform device drivers to bind to. Platform device drivers use the `ZX_PROTOCOL_PLATFORM_DEV` protocol
(see [system/ulib/ddk/include/ddk/protocol/platform-device.h](../../system/ulib/ddk/include/ddk/protocol/platform-device.h))
to communicate with the platform bus.

The platform device protocol is used by platform device drivers to access the resources they need
to function.
To allow development of platform independent drivers, these resources are accessed via device
specific index rather than an a physical address or ID in the global name space.
For example, if a driver needs two IRQs, they will be index zero and one rather than the actual IRQ
numbers on the platform.
Similarly, MMIO ranges are accessed via an index rather than the actual physical address.
This allows us to write drivers that work across multiple platforms, where the basic functionality
of the hardware is the same but details like MMIO addresses and IRQ and GPIO numbers may be
different. The mapping from the indices used by the platform devices and the actual physical values
is provided by the board driver.

Platform device drivers use `pdev_map_mmio()` to map MMIO regions and `pdev_map_interrupt()`
to get interrupt handles for their IRQs. The platform device protocol also provides
`pdev_alloc_contig_vmo()` and `pdev_map_contig_vmo()` to create contiguous VMOs for hardware that
requires contiguous DMA buffers. (It is strongly encouraged to use regular non-contiguous VMOs
whenever possible instead). Finally, the platform device protocol includes `pdev_vmo_buffer_t`,
which is a helper to make it easy to work with VMOs for MMIO ranges and contiguous DMA regions.

## Platform Bus Protocols

In addition to the `ZX_PROTOCOL_PLATFORM_DEV` platform device protocol, the platform bus can provide
additional protocols to platform device drivers. These protocols are also accessed via the DDK
`device_get_protocol` API. For example, the platform bus can provide the `ZX_PROTOCOL_GPIO`
protocol for GPIOs and `ZX_PROTOCOL_I2C` for devices on an I2C bus.
The implementation of these protocols live in the board driver.
The board driver provides this via the `pbus_interface_get_protocol` call in the interface
that the board driver registers with the platform bus.
For these protocols, the resources are also accessed via an abstract index rather than raw
value to insulate the platform device driver from the specifics of the platform.
