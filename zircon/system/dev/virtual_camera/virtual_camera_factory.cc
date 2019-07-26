// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtual_camera_factory.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

namespace virtual_camera {

namespace fuchsia = ::llcpp::fuchsia;

zx_status_t VirtualCameraFactory::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto manager = fbl::make_unique_checked<VirtualCameraFactory>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = manager->DdkAdd("virtual_camera_factory");
  if (status != ZX_OK) {
    zxlogf(ERROR, "virtual_camera_manager: Could not add virtual camera  manager: %d\n", status);
    return status;
  }

  __UNUSED auto* dev = manager.release();
  return ZX_OK;
}

void VirtualCameraFactory::DdkUnbind() { DdkRemove(); }

void VirtualCameraFactory::DdkRelease() { delete this; }

void VirtualCameraFactory::CreateDevice(
    fuchsia::camera::common::VirtualCameraConfig config,
    fuchsia::camera::common::VirtualCameraFactory::Interface::CreateDeviceCompleter::Sync
        completer) {
  // TODO(eweeks): Implement this.
}

static constexpr zx_driver_ops_t kDriverOps = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = VirtualCameraFactory::Create;
  return ops;
}();

}  // namespace virtual_camera

// clang-format off
ZIRCON_DRIVER_BEGIN(virtual_factory, virtual_camera::kDriverOps, "vfactory", "0.1", 3)
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_VCAMERA_TEST),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_VCAM_FACTORY),
ZIRCON_DRIVER_END(virtual_factory)
    // clang-format on
