// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver-unit-test/utils.h>
#include <lib/syslog/cpp/macros.h>

#include "src/camera/drivers/hw_accel/ge2d/ge2d-bind.h"
#include "src/camera/drivers/hw_accel/ge2d/ge2d.h"

namespace ge2d {
namespace {

constexpr auto kTag = "ge2d";
}
zx_status_t Ge2dBind(void* ctx, zx_device_t* device) {
  std::unique_ptr<Ge2dDevice> ge2d_device;
  zx_status_t status = ge2d::Ge2dDevice::Setup(device, &ge2d_device);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Could not setup ge2d device";
    return status;
  }
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_PROTO, 0, ZX_PROTOCOL_GE2D},
  };

  status = ge2d_device->DdkAdd(ddk::DeviceAddArgs("ge2d").set_props(props));
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Could not add ge2d device";
    return status;
  }

  FX_LOGST(INFO, kTag) << "ge2d driver added";

  // ge2d device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto* dev = ge2d_device.release();
  return status;
}

bool run_unit_tests(void* ctx, zx_device_t* parent, zx_handle_t channel) {
  return driver_unit_test::RunZxTests("Ge2dTests", parent, channel);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Ge2dBind;
  ops.run_unit_tests = run_unit_tests;
  return ops;
}();

}  // namespace ge2d

ZIRCON_DRIVER(ge2d, ge2d::driver_ops, "ge2d", "0.1");
