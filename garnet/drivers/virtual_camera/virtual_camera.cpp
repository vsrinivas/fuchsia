// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>

#include <ddk/debug.h>

#include <memory>

#include "garnet/drivers/virtual_camera/virtual_camera_device.h"

namespace virtual_camera {

static zx_status_t bind(void* ctx, zx_device_t* device) {
  auto dev = std::make_unique<VirtualCameraDevice>();
  zx_status_t status = dev->Bind(device);
  if (status != ZX_OK) {
    zxlogf(ERROR, "*** virtual_camera: could not add device '%s': %d\n",
           __func__, status);
    return status;
  }

  // On successful Add, Devmgr takes ownership (relinquished on DdkRelease),
  // so transfer our ownership to a local var, and let it go out of scope.
  auto __UNUSED temp_ref = dev.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = bind;
    return ops;
}();

}  // namespace virtual_camera

// clang-format: off
ZIRCON_DRIVER_BEGIN(virtual_camera, virtual_camera::driver_ops, "fuchsia", "0.1", 1)
  BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),
ZIRCON_DRIVER_END(virtual_camera)
