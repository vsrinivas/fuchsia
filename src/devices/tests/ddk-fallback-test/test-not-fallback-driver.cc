// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <zircon/errors.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

#include "src/devices/tests/ddk-fallback-test/test-driver-bind.h"

namespace {

class TestNormalDriver;
using DeviceType = ddk::Device<TestNormalDriver, ddk::Unbindable>;

class TestNormalDriver : public DeviceType {
 public:
  explicit TestNormalDriver(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Bind() { return DdkAdd("ddk-not-fallback-test"); }

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }
};

zx_status_t TestNormalBind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestNormalDriver>(&ac, device);
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
  ops.bind = TestNormalBind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(ddk_not_fallback_test, driver_ops, "zircon", "0.1");
