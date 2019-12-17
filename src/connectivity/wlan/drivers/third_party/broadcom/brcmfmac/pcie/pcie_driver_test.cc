// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_driver_test.h"

#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include <ddk/driver.h>
#include <ddk/protocol/pci.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chip.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipcommon.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_buscore.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_regs.h"
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
  zx_status_t DeviceGetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual) override;
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

zx_status_t StubDdkDevice::DeviceGetMetadata(uint32_t type, void* buf, size_t buflen,
                                             size_t* actual) {
  return device_get_metadata(parent(), type, buf, buflen, actual);
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

  // Check the PcieBuscore::CoreRegs locking model.
  {
    // Hold a CoreRegs instance for PCIE2_CORE.  We should then fail getting a conflicting
    // ARM_CR4_CORE instance, but succeed in getting a simultaneous PCIE2_CORE instance.
    {
      PcieBuscore::CoreRegs pcie2_core_coreregs;
      if (pcie2_core_coreregs.is_valid()) {
        BRCMF_ERR("Empty CoreRegs instance should not be valid\n");
        return ZX_ERR_BAD_STATE;
      }
      if ((status = pcie_buscore->GetCoreRegs(CHIPSET_PCIE2_CORE, &pcie2_core_coreregs)) != ZX_OK) {
        BRCMF_ERR("Failed to get CoreRegs instance for PCIE2_CORE: %s\n",
                  zx_status_get_string(status));
        return status;
      }
      PcieBuscore::CoreRegs arm_cr4_coreregs;
      if ((status = pcie_buscore->GetCoreRegs(CHIPSET_ARM_CR4_CORE, &arm_cr4_coreregs)) == ZX_OK) {
        BRCMF_ERR("Got conflicting CoreRegs instance for ARM_CR4_CORE: %s\n",
                  zx_status_get_string(status));
        return status;
      }
      PcieBuscore::CoreRegs pcie2_core_coreregs_2;
      if ((status = pcie_buscore->GetCoreRegs(CHIPSET_PCIE2_CORE, &pcie2_core_coreregs_2)) !=
          ZX_OK) {
        BRCMF_ERR("Failed to get simultaneous CoreRegs instance for PCIE2_CORE: %s\n",
                  zx_status_get_string(status));
        return status;
      }
      // Perform a smoke-test read.
      pcie2_core_coreregs_2.RegRead(BRCMF_PCIE_PCIE2REG_INTMASK);
    }

    // Now that the CoreRegs instance for PCIE2_CORE has dropped out of scope, we can get
    // ARM_CR4_CORE.
    PcieBuscore::CoreRegs arm_cr4_coreregs;
    if ((status = pcie_buscore->GetCoreRegs(CHIPSET_ARM_CR4_CORE, &arm_cr4_coreregs)) != ZX_OK) {
      BRCMF_ERR("Failed to get CoreRegs instance for ARM_CR4_CORE: %s\n",
                zx_status_get_string(status));
      return status;
    }
  }

  // Smoke-test CPU access to DMA-visible memory.
  constexpr size_t kDmaBufferSize = 8 * 1024;
  std::unique_ptr<DmaBuffer> dma_buffer;
  if ((status = pcie_buscore->CreateDmaBuffer(ZX_CACHE_POLICY_CACHED, kDmaBufferSize,
                                              &dma_buffer)) != ZX_OK) {
    BRCMF_ERR("DMA buffer creation failed: %s\n", zx_status_get_string(status));
    return status;
  }
  if ((status = dma_buffer->Map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE)) != ZX_OK) {
    BRCMF_ERR("DMA buffer map failed: %s\n", zx_status_get_string(status));
    return status;
  }
  if (!dma_buffer->is_valid() || dma_buffer->dma_address() == 0 ||
      dma_buffer->size() != kDmaBufferSize) {
    BRCMF_ERR("PcieBuscore created invalid DMA buffer: is_valid=%d, dma_address=0x%zx, size=%zu\n",
              dma_buffer->is_valid(), static_cast<size_t>(dma_buffer->dma_address()),
              dma_buffer->size());
    return ZX_ERR_NO_RESOURCES;
  }
  std::vector dma_data(kDmaBufferSize, '\x80');
  std::memcpy(reinterpret_cast<void*>(dma_buffer->address()), dma_data.data(), kDmaBufferSize);
  std::vector dma_read_data(kDmaBufferSize, '\0');
  std::memcpy(dma_read_data.data(), reinterpret_cast<void*>(dma_buffer->address()), kDmaBufferSize);
  if (!std::equal(dma_data.begin(), dma_data.end(), dma_read_data.begin())) {
    BRCMF_ERR("DMA buffer read did not return written data\n");
    return ZX_ERR_IO_DATA_INTEGRITY;
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
