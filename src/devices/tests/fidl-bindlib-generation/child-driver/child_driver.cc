// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/fidl-bindlib-generation/child-driver/child_driver.h"

#include "src/devices/tests/fidl-bindlib-generation/child-driver/child_driver-bind.h"

namespace child_driver {

zx_status_t ChildDriver::Bind(void* ctx, zx_device_t* dev) {
  auto device = std::make_unique<ChildDriver>(dev);
  device->is_bound.Set(true);

  auto child_args = ddk::DeviceAddArgs("child").set_inspect_vmo(device->inspect_vmo());
  zx_status_t status = device->DdkAdd(child_args);
  if (status != ZX_OK) {
    return status;
  }

  __UNUSED auto ptr = device.release();
  return ZX_OK;
}

void ChildDriver::DdkRelease() { delete this; }

static zx_driver_ops_t child_driver_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ChildDriver::Bind;
  return ops;
}();

}  // namespace child_driver

ZIRCON_DRIVER(ChildDriver, child_driver::child_driver_driver_ops, "zircon", "0.1");
