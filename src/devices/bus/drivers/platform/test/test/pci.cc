// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <memory>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-pci-bind.h"
#include "src/devices/pci/testing/pci_protocol_fake.h"

namespace pci {

class TestPciDevice;
using DeviceType = ddk::Device<TestPciDevice, ddk::Unbindable>;

class TestPciDevice : public DeviceType, public FakePciProtocol {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit TestPciDevice(zx_device_t* parent) : DeviceType(parent) {}

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
};

zx_status_t TestPciDevice::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<TestPciDevice>(parent);

  pcie_device_info_t info = {.device_id = PDEV_DID_TEST_PCI };
  dev->SetDeviceInfo(info);

  auto status = dev->DdkAdd("test-pci");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }

  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void TestPciDevice::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void TestPciDevice::DdkRelease() { delete this; }

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = TestPciDevice::Create;
  return driver_ops;
}();

}  // namespace pci

ZIRCON_DRIVER(test_pci, pci::driver_ops, "zircon", "0.1");
