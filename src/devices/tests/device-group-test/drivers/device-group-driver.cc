// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/device-group-test/drivers/device-group-driver.h"

#include "src/devices/tests/device-group-test/drivers/device-group-driver-bind.h"

namespace device_group_driver {

// static
zx_status_t DeviceGroupDriver::Bind(void* ctx, zx_device_t* device) {
  auto dev = std::make_unique<DeviceGroupDriver>(device);

  auto status = dev->DdkAdd("device_group");
  if (status != ZX_OK) {
    return status;
  }

  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

static zx_driver_ops_t kDriverOps = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = DeviceGroupDriver::Bind;
  return ops;
}();

}  // namespace device_group_driver

ZIRCON_DRIVER(device_group_driver, device_group_driver::kDriverOps, "zircon", "0.1");
