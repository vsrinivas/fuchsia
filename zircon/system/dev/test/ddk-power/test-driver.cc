// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/alloc_checker.h>

class TestPowerDriver;
using DeviceType = ddk::Device<TestPowerDriver, ddk::Unbindable>;
class TestPowerDriver : public DeviceType,
                                    public ddk::EmptyProtocol<ZX_PROTOCOL_TEST_POWER_CHILD> {
 public:
  TestPowerDriver(zx_device_t* parent) : DeviceType(parent) {}
  zx_status_t Bind();
  void DdkUnbind() { DdkRemove(); }
  void DdkRelease() { delete this; }
};

zx_status_t TestPowerDriver::Bind() {
  return DdkAdd("power-test");
}

zx_status_t test_power_hook_bind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestPowerDriver>(&ac, device);
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

static zx_driver_ops_t test_power_hook_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops;
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = test_power_hook_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(TestPower, test_power_hook_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_POWER_TEST),
ZIRCON_DRIVER_END(TestPower)
// clang-format on
