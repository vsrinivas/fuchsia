// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <ddktl/device.h>

#include "src/devices/tests/fidl-protocol/isolated-child-driver-bind.h"

namespace {

class IsolatedDevice;
using IsolatedDeviceType = ddk::Device<IsolatedDevice>;

// Test driver whose purpose is to call DdkAdd with DEVICE_ADD_MUST_ISOLATE so the test can ensure
// that the flag is passed from the driver host to the driver manager.
class IsolatedDevice : public IsolatedDeviceType {
 public:
  IsolatedDevice(zx_device_t* parent) : IsolatedDeviceType(parent) {}

  zx_status_t Bind() {
    return DdkAdd(ddk::DeviceAddArgs("isolated-child").set_flags(DEVICE_ADD_MUST_ISOLATE));
  }

  void DdkRelease() { delete this; }
};

zx_status_t bind(void* ctx, zx_device_t* parent_device) {
  auto device = std::make_unique<IsolatedDevice>(parent_device);
  zx_status_t status = device->Bind();
  if (status == ZX_OK) {
    __UNUSED auto ptr = device.release();
  }
  return status;
}

static constexpr zx_driver_ops_t kDriverOps = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = bind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(fidl_protocol_test_isolated, kDriverOps, "zircon", "0.1");
