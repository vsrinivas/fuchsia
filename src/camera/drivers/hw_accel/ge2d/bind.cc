// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>

#include "ge2d.h"
#include "src/camera/drivers/hw_accel/ge2d/test/ge2d-on-device-test.h"
#include "src/lib/syslog/cpp/logger.h"

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

  status = ge2d_device->DdkAdd("ge2d", 0, props, countof(props));
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Could not add ge2d device";
    return status;
  }

  FX_LOG(INFO, kTag, "ge2d driver added");

  // ge2d device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto* dev = ge2d_device.release();
  return status;
}

bool run_unit_tests(void* ctx, zx_device_t* parent, zx_handle_t channel) {
  std::unique_ptr<Ge2dDevice> ge2d_device;
  zx_status_t status = ge2d::Ge2dDevice::Setup(parent, &ge2d_device);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Could not setup ge2d device";
    return false;
  }

  // Run the unit tests for this device
  // Can be enabled by setting the driver.ge2d.tests.enable=true command-line arg.
  status = ge2d::Ge2dDeviceTester::RunTests(ge2d_device.get());
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Device Unit Tests Failed");
    return false;
  }
  return true;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Ge2dBind;
  ops.run_unit_tests = run_unit_tests;
  return ops;
}();

}  // namespace ge2d

// clang-format off
ZIRCON_DRIVER_BEGIN(ge2d, ge2d::driver_ops, "ge2d", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_GE2D),
ZIRCON_DRIVER_END(ge2d)
