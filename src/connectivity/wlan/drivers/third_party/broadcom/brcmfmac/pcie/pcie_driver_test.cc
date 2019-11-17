// Copyright (c) 2019 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_driver_test.h"

#include <zircon/errors.h>
#include <zircon/status.h>

#include <memory>

#include <ddk/driver.h>
#include <ddk/protocol/pci.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chip.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipcommon.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_buscore.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/soc.h"

namespace wlan {
namespace brcmfmac {
namespace {

// A stub implementation of Device that just provides the Zircon DDK entry points.
class StubDdkDevice : public Device {
 public:
  explicit StubDdkDevice(zx_device_t* device);
  ~StubDdkDevice();

  // Trampolines for DDK functions, for platforms that support them
  zx_status_t DeviceAdd(device_add_args_t* args, zx_device_t** out_device) override;
  void DeviceAsyncRemove(zx_device_t* dev) override;
  zx_status_t LoadFirmware(const char* path, zx_handle_t* fw, size_t* size) override;
};

StubDdkDevice::StubDdkDevice(zx_device_t* device) : Device(device) { Device::Init(); }

StubDdkDevice::~StubDdkDevice() = default;

zx_status_t StubDdkDevice::DeviceAdd(device_add_args_t* args, zx_device_t** out_device) {
  return device_add(parent(), args, out_device);
}

void StubDdkDevice::DeviceAsyncRemove(zx_device_t* dev) { device_async_remove(dev); }

zx_status_t StubDdkDevice::LoadFirmware(const char* path, zx_handle_t* fw, size_t* size) {
  return load_firmware(parent(), path, fw, size);
}

// The following are unit tests for the individual components of the PCIE driver.  The testing
// strategy is such that, for the case of the components:
//
// +--------------------------+
// | Object A: -> owns        |
// |   Object B: -> [...]     |
// |   Object C: -> [...]     |
// | +----------------------+ |
// | | Object D: -> owns:   | |
// | |   Object X: -> [...] | |
// | |   Object Y: -> [...] | |
// | |   Object Z: -> [...] | |
// | +----------------------+ |
// +--------------------------+
//
// Object D will have a test named "Run<D>ComponentsTest()", which will unit-test its components
// { X, Y, Z }.  Its parent component Object A will also have test named "Run<A>ComponentsTest()",
// which will unit-test its components { B, C, D }, while serving also as an integration test for D.
// This pattern continued recursively ensures that each component has both unit and integration
// tests.

// Unit tests for the components of PcieBus.
zx_status_t RunPcieBusComponentsTest(zx_device_t* parent) {
  zx_status_t status = ZX_OK;

  std::unique_ptr<PcieBuscore> pcie_buscore;
  if ((status = PcieBuscore::Create(parent, &pcie_buscore)) != ZX_OK) {
    BRCMF_ERR("PcieBuscore creation failed: %s\n", zx_status_get_string(status));
    return status;
  }

  // Make sure the bus chip was brought up successfully.
  const auto chip = pcie_buscore->chip();
  if (chip == nullptr) {
    BRCMF_ERR("No bus chip found\n");
    return ZX_ERR_NO_RESOURCES;
  }
  if (chip->chip == 0) {
    BRCMF_ERR("No bus chip ID found\n");
    return ZX_ERR_NO_RESOURCES;
  }
  BRCMF_INFO("Found bus chip 0x%x rev. %d\n", chip->chip, chip->chiprev);

  // Check that we can do a register read to confirm the bus chip information.
  const uint32_t chipreg = PcieBuscore::GetBuscoreOps()->read32(
      static_cast<void*>(pcie_buscore.get()), CORE_CC_REG(SI_ENUM_BASE, chipid));
  const uint32_t chipreg_chip = (chipreg & CID_ID_MASK);
  const uint32_t chipreg_rev = ((chipreg & CID_REV_MASK) >> CID_REV_SHIFT);
  if (chip->chip != chipreg_chip || chip->chiprev != chipreg_rev) {
    BRCMF_ERR("Bus chip read returned chip 0x%x rev. %d, expected 0x%x rev. %d\n", chipreg_chip,
              chipreg_rev, chip->chip, chip->chiprev);
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

// Unit tests for the components of PcieDevice.
zx_status_t RunPcieDeviceComponentsTest(zx_device_t* parent) {
  zx_status_t status = ZX_OK;
  StubDdkDevice device(parent);

  std::unique_ptr<PcieBus> pcie_bus;
  if ((status = PcieBus::Create(&device, &pcie_bus)) != ZX_OK) {
    BRCMF_ERR("PcieBus creation failed: %s\n", zx_status_get_string(status));
    return status;
  }

  // Check that the chip bringup was successful.
  if (pcie_bus->GetBusOps()->get_ramsize(device.drvr()->bus_if) == 0) {
    BRCMF_ERR("PcieBus returned 0 ramsize\n");
    return ZX_ERR_NO_RESOURCES;
  }

  return ZX_OK;
}

}  // namespace

// Main entry point to runtime driver tests.
zx_status_t RunPcieDriverTest(zx_device_t* parent) {
  zx_status_t status = ZX_OK;

  pci_protocol_t pci = {};
  if (device_get_protocol(parent, ZX_PROTOCOL_PCI, &pci) == ZX_OK) {
    BRCMF_INFO("Running PCIE tests\n");

    // Unit tests for the different components of the driver.
    if ((status = RunPcieBusComponentsTest(parent)) != ZX_OK) {
      BRCMF_ERR("PCIE bus components test failed: %s\n", zx_status_get_string(status));
      return status;
    }
    if ((status = RunPcieDeviceComponentsTest(parent)) != ZX_OK) {
      BRCMF_ERR("PCIE device components test failed: %s\n", zx_status_get_string(status));
      return status;
    }

    // One overall integration test for the entire driver.
    PcieDevice* device = nullptr;
    if ((status = PcieDevice::Create(parent, &device)) != ZX_OK) {
      BRCMF_ERR("PCIE device integration test failed: %s\n", zx_status_get_string(status));
      return status;
    }
    device->DdkAsyncRemove();

    BRCMF_INFO("PCIE tests passed\n");
  }

  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan
