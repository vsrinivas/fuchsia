// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_bus_driver.h"
#include "../../config.h"
#include "../../device.h"
#include "driver_tests.h"
#include <ddk/binding.h>
#include <ddk/platform-defs.h>

namespace pci {

zx_status_t FakeBusDriver::Create(zx_device_t* parent, const char* name) {
    std::unique_ptr<FakePciroot> root;
    zx_status_t st = FakePciroot::Create(0, 1, &root);
    if (st != ZX_OK) {
        return st;
    }
    auto bus = std::unique_ptr<FakeBusDriver>(new FakeBusDriver(parent, std::move(root)));
    st = bus->DdkAdd(name);
    if (st != ZX_OK) {
        return st;
    }

    auto& dev = bus->GetDevice(bus->test_bdf());
    dev.set_vendor_id(PCI_TEST_DRIVER_VID).set_device_id(PCI_TEST_DRIVER_DID);
    st = bus->CreateDevice(bus->test_bdf());
    if (st != ZX_OK) {
        bus->DdkRemove();
        return st;
    }

    bus.release();
    return ZX_OK;
}

zx_status_t FakeBusDriver::CreateDevice(pci_bdf_t bdf) {
    fbl::RefPtr<Config> cfg;
    zx_status_t st = MmioConfig::Create(bdf, &pciroot().ecam().get_mmio(), 0, 1, &cfg);
    if (st != ZX_OK) {
        return st;
    }

    st = pci::Device::Create(this->zxdev(), std::move(cfg), &upstream(), bus().bli());
    if (st != ZX_OK) {
        return st;
    }

    return ZX_OK;
}

} // namespace pci

static zx_status_t fake_pci_bus_driver_bind(void* ctx, zx_device_t* parent) {
    return pci::FakeBusDriver::Create(parent, kFakeBusDriverName);
}

static zx_driver_ops_t fake_pci_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = nullptr,
    .bind = fake_pci_bus_driver_bind,
    .create = nullptr,
    .release = nullptr,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(fake_pci_bus_driver, fake_pci_bus_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PCI_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, 0),
    BI_MATCH()
ZIRCON_DRIVER_END(fake_pci_bus_driver)
