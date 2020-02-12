# brcmfmac PCIE Components

These components are the implementation of the PCIE bus interface for the brcmfmac driver.  These
are the base components of the driver; during initialization, the driver binding code instantiates
a `PcieDriver` instance, which owns and is responsible for the lifetime of all the other driver
components.


## Structure

### `PcieDevice`

The [`PcieDevice`](pcie_device.h) class is the interface between the brcmfmac driver and the DDK
concepts and entry points.  As a `ddk::WlanphyImplProtocol<>` subclass, it presents the DDK view of a
WLAN device as a `WlanphyImpl` device providing multiple `WlanifImpl` interfaces.  As a
[`Device`](../device.h) subclass, it provides the brcmfmac driver with access to the relevant DDK
entry points.

Its data members are:

#### `MsgbufProto`

This class is defined in the [../msgbuf/] directory, and contains the implementation of the MSGBUF
protocol, used by the higher-level `core` and `cfg80211` driver components.  The MSGBUF components
interact with the PCIE layer through through a set of interfaces defined at
[../msgbuf/msgbuf_interfaces.h].

#### `PcieBus`

The [`PcieBus`](pcie_bus.h) class contains the low-level PCIE bus components.  It exposes two
interfaces:

* The [`brcmf_bus_ops`](../bus.h) interface, used by the rest of the brcmfmac driver.
* The [MSGBUF interfaces](../msgbuf/msgbuf_interfaces.h), used by the MSGBUF layer.

Its data members are:

##### `PcieBuscore`

The [`PcieBuscore`](pcie_buscore.h) class contains the lowest-level PCIE logic, responsible for
bringing up the bus and establishing communication with the brcmfmac chipset behind the bus.  It
creates and maintains the PCIE BAR windows, DMA memory regions, and mediates memory-mapped register
access.

Of particular note is the `CoreRegs` subclass.  Communication with individual chipset cores behind
the PCIE bus is done through a special window in the BAR mapping, which is a shared resource.
`CoreRegs` provides thread-safe access to this window in an idiomatic RAII fashion; as long as a
`CoreRegs` instance is held, then access to the window using the instance is safe.

This class also implements [`DmaBufferProviderInterface`](../msgbuf/msgbuf_interfaces.h), for use
with the MSGBUF layer.

##### `PcieFirmware`

The [`PcieFirmware`](pcie_firmware.h) class handles firmware loading and interpretation of firmware
settings.  It also contains logic for reading the firmware debug console log.

##### `PcieRingMaster`

The [`PcieRingMaster`](pcie_ring_master.h) class manages the DMA rings which are used to send
commands and TX packets to the device, and receive completions and RX packets from the device.  The
command, configuration, and completion rings are fixed and owned by the `PcieRingMaster` instance;
TX flow rings are allocated dynamically and ownership is returned with the ring.

This class also implements the [`DmaRingProviderInterface`](../msgbuf/msgbuf_interfaces.h), for use
with the MSGBUF layer.

##### `PcieInterruptMaster`

The [`PcieInterruptMaster`](pcie_interrupt_master.h) class maintains the interrupt thread which
receives interrupts from the PCIE bus and dispatches them onward to the driver.  Driver components
can register a handler with the `PcieInterruptMaster`, to be notified when an interrupt occurs.

This class also implements the [`DmaInterruptProviderInterface`](../msgbuf/msgbuf_interfaces.h), for
use with the MSGBUF layer.
