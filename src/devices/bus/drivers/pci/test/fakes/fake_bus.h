// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_BUS_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_BUS_H_

#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/ddk/mmio-buffer.h>
#include <lib/zx/msi.h>
#include <lib/zx/resource.h>
#include <zircon/hw/pci.h>

#include <hwreg/bitfields.h>

#include "src/devices/bus/drivers/pci/bus.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_pciroot.h"

namespace pci {

class FakeBus : public BusDeviceInterface {
 public:
  explicit FakeBus(uint8_t bus_start = 0, uint8_t bus_end = 0) : pciroot_(bus_start, bus_end) {}

  zx_status_t LinkDevice(fbl::RefPtr<pci::Device> device) final {
    fbl::AutoLock devices_lock(&devices_lock_);
    devices_.insert(device);
    return ZX_OK;
  }

  zx_status_t UnlinkDevice(pci::Device* device) final {
    devices_.erase(*device);
    return ZX_OK;
  }

  zx_status_t AllocateMsi(uint32_t count, zx::msi* msi) final {
    fbl::AutoLock devices_lock(&devices_lock_);
    // Using fake MSIs supplied by lib/fake-msi
    return zx::msi::allocate(*zx::unowned_resource(ZX_HANDLE_INVALID), count, msi);
  }

  zx_status_t GetBti(const pci::Device* /*device*/, uint32_t /*index*/, zx::bti* /*bti*/) final {
    fbl::AutoLock devices_lock(&devices_lock_);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t ConnectSysmem(zx::channel channel) final { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t AddToSharedIrqList(pci::Device* device, uint32_t vector) final {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t RemoveFromSharedIrqList(pci::Device* device, uint32_t vector) final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  pci::Device& get_device(pci_bdf_t bdf) { return *devices_.find(bdf); }

  // For use with Devices that need to link to a Bus.
  BusDeviceInterface* bdi() { return static_cast<BusDeviceInterface*>(this); }

  const pci::DeviceTree& devices() { return devices_; }
  FakePciroot& pciroot() { return pciroot_; }

 private:
  fbl::Mutex devices_lock_;
  pci::DeviceTree devices_;
  FakePciroot pciroot_;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_BUS_H_
