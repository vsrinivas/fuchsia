// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <sstream>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

#include "src/devices/tests/ddk-fallback-test-v2/test-driver-bind.h"

namespace {

class TestFallbackDriver;
using DeviceType = ddk::Device<TestFallbackDriver>;

class TestFallbackDriver : public DeviceType {
 public:
  explicit TestFallbackDriver(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Bind() {
    // Generate a unique child device name, in case the fallback driver is bound
    // multiple times.
    std::string name = std::string("ddk-fallback-test-device-").append(std::to_string(counter_));
    return DdkAdd(name.c_str());
  }

  // Device protocol implementation.
  void DdkRelease() { delete this; }

 private:
  // A counter to generate unique child device names
  inline static int counter_ = 0;
};

zx_status_t TestFallbackBind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestFallbackDriver>(&ac, device);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

zx_driver_ops_t driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestFallbackBind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(ddk_fallback_test, driver_ops, "zircon", "0.1");
