// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_bus_driver.h"

#include <lib/ddk/platform-defs.h>
#include <lib/fit/defer.h>

#include "src/devices/bus/drivers/pci/config.h"
#include "src/devices/bus/drivers/pci/device.h"
#include "src/devices/bus/drivers/pci/test/driver/driver_tests.h"
#include "src/devices/bus/drivers/pci/test/driver/fake_pci_bus_driver_bind.h"
#include "src/devices/bus/drivers/pci/test/fakes/test_device.h"

namespace pci {

zx_status_t FakeBusDriver::Create(zx_device_t* parent, const char* name, uint8_t start_bus,
                                  uint8_t end_bus) {
  auto bus_driver = std::unique_ptr<FakeBusDriver>(new FakeBusDriver(parent, start_bus, end_bus));
  zx_status_t st = bus_driver->DdkAdd(name);
  if (st != ZX_OK) {
    return st;
  }

  auto cleanup = fit::defer([&bus_driver] { bus_driver->DdkAsyncRemove(); });
  st = bus_driver->CreateDevice(bus_driver->test_bdf(), kFakeQuadroDeviceConfig.data(),
                                kFakeQuadroDeviceConfig.max_size());
  if (st != ZX_OK) {
    return st;
  }

  bus_driver->upstream().ConfigureDownstreamDevices();
  cleanup.cancel();
  bus_driver.release();
  return ZX_OK;
}

// Creates a device, seeding the configuration space with a given buffer if provided.
zx_status_t FakeBusDriver::CreateDevice(pci_bdf_t bdf, uint8_t* base_cfg, size_t base_cfg_size,
                                        uint16_t vid, uint16_t did) {
  ddk::MmioView view = bus_.pciroot().ecam().mmio().View(bdf_to_ecam_offset(bdf, 0), ZX_PAGE_SIZE);
  for (uint32_t off = 0; off < base_cfg_size; off++) {
    view.Write(base_cfg[off], off);
  }

  std::unique_ptr<Config> cfg = std::make_unique<FakeMmioConfig>(bdf, std::move(view));
  cfg->Write(Config::kVendorId, vid);
  cfg->Write(Config::kDeviceId, did);
  return pci::Device::Create(this->zxdev(), std::move(cfg), &upstream(), bus().bdi(),
                             inspector_.GetRoot().CreateChild(cfg->addr()));
}

}  // namespace pci

static zx_status_t fake_pci_bus_driver_bind(void* ctx, zx_device_t* parent) {
  return pci::FakeBusDriver::Create(parent, kFakeBusDriverName);
}

static const zx_driver_ops_t fake_pci_bus_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = fake_pci_bus_driver_bind;
  return ops;
}();

ZIRCON_DRIVER(fake_pci_bus_driver, fake_pci_bus_driver_ops, "zircon", "0.1");
