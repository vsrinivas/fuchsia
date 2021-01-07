// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/rpmb/cpp/banjo.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-rpmb-bind.h"

namespace rpmb {

class TestRpmbDevice;
using DeviceType = ddk::Device<TestRpmbDevice, ddk::Unbindable>;

class TestRpmbDevice : public DeviceType,
                       public ddk::RpmbProtocol<TestRpmbDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit TestRpmbDevice(zx_device_t* parent) : DeviceType(parent) {}

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  void RpmbConnectServer(zx::channel server);
};

zx_status_t TestRpmbDevice::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<TestRpmbDevice>(parent);

  zxlogf(INFO, "TestRpmbDevice::Create");

  auto status = dev->DdkAdd("test-rpmb");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }

  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void TestRpmbDevice::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void TestRpmbDevice::DdkRelease() { delete this; }

void TestRpmbDevice::RpmbConnectServer(zx::channel server) {}

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = TestRpmbDevice::Create;
  return driver_ops;
}();

}  // namespace rpmb

ZIRCON_DRIVER(test_rpmb, rpmb::driver_ops, "zircon", "0.1");
