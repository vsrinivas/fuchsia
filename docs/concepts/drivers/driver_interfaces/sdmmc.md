# SDMMC drivers architecture

The SDMMC driver stack is divided into two main components: platform drivers
which talk directly to controller hardware, and the core driver which handles
protocol-specific device initialization and communication. The core driver is
further divided into an SDIO driver and a block driver (for SD and eMMC). Each
SDMMC controller has a different platform driver, while the core driver is
used on all platforms.

## Bringup

Bringing up an SoC with a new SDMMC controller requires writing a new platform
driver. If the controller implements the SDHCI specification then this driver
should implement
[ddk.protocol.sdhci](/sdk/banjo/ddk.protocol.sdhci/sdhci.banjo), otherwise it
should implement
[ddk.protocol.sdmmc](/sdk/banjo/ddk.protocol.sdmmc/sdmmc.banjo). It may be
helpful to disable DMA and higher speed modes through `SdmmcHostInfo` and
`SdmmcHostPrefs` until the basic functionality of the hardware has been
validated. See the SDHCI and SDMMC protocol definitions for more information.

## SD/eMMC core driver

The SD/eMMC block driver creates a device that implements
[ddk.protocol.block.BlockImpl](/sdk/banjo/ddk.protocol.block/block.banjo) and
[ddk.protocol.block.partition](/sdk/banjo/ddk.protocol.block.partition/partition.banjo)
for the user data partition, as well as devices for the boot0 and boot1
partitions if enabled (eMMC only). A device implementing
[ddk.protocol.rpmb](/sdk/banjo/ddk.protocol.rpmb/rpmb.banjo) is created if the
device supports it (eMMC only, based on JEDEC standard JESD84-B51 section 6.6.22).

## SDIO core driver

The SDIO core driver creates devices that implement
[ddk.protocol.sdio](/sdk/banjo/ddk.protocol.sdio/sdio.banjo), one for
each IO function. Whereas the only expected client of the SD/eMMC block driver
is the storage stack, the SDIO driver will have different clients depending on
what kind of SDIO card is detected. Client drivers bind to the SDIO core driver
using the bind variables specified in the table below. Client drivers that use
more than one IO function should bind to a composite device that has each
function device as a fragment. Note that there could be multiple concurrent SDIO
client drivers for combo cards, e.g. for Bluetooth and WiFi, in which case
access to the bus will be shared through the core driver. Clients also cannot
directly access function 0 to prevent possibly disrupting other clients. See the
SDIO protocol definition for more information.

### SDIO client binding

| Bind variable        | Meaning                                               |
| ---------------------| ------------------------------------------------------|
| `BIND_SDIO_VID`      | The IO function's manufacturer ID read from FBR       |
| `BIND_SDIO_PID`      | The IO function's product ID read from FBR            |
| `BIND_SDIO_FUNCTION` | The IO function number from 1 to 7                    |

## Device diagram

![SDMMC device diagram](images/sdmmc_architecture.png)
