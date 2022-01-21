// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <memory>

#include <ddktl/device.h>

#include "src/lib/isolated_devmgr/test-device-bind.h"

namespace test {

class TestDevice;

using TestDeviceType = ddk::Device<TestDevice>;

class TestDevice : public TestDeviceType {
 public:
  explicit TestDevice(zx_device_t* parent) : TestDeviceType(parent) {}

  void DdkRelease() { delete this; }

  static zx_status_t Create(void* ctx, zx_device_t* parent) {
    zxlogf(INFO, "TestDevice::Create");
    auto dev = std::unique_ptr<TestDevice>(new TestDevice(parent));
    zx_status_t status = dev->DdkAdd("test-device");
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not add device: %d", __func__, status);
    } else {
      // devmgr owns the memory now
      __UNUSED auto* ptr = dev.release();
    }
    return status;
  }
};

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestDevice::Create;
  return ops;
}();

}  // namespace test

ZIRCON_DRIVER(test_device, test::driver_ops, "fuchsia", "0.1");
