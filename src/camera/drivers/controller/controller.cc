// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller.h"

#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>

namespace camera {

namespace {

enum {
  COMPONENT_ISP,
  COMPONENT_GDC,
  COMPONENT_COUNT,
};

}
void Controller::DdkUnbind() {
  ShutDown();
  DdkRemove();
}

void Controller::DdkRelease() { delete this; }

void Controller::ShutDown() {}

zx_status_t Controller::StartThread() {
  return controller_loop_.StartThread("camera-controller-loop", &loop_thread_);
}

// static
zx_status_t Controller::Setup(zx_device_t* parent, std::unique_ptr<Controller>* out) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s: could not get composite protocoln", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_device_t* components[COMPONENT_COUNT];
  size_t actual;
  composite.GetComponents(components, COMPONENT_COUNT, &actual);
  if (actual != COMPONENT_COUNT) {
    zxlogf(ERROR, "%s: Could not get components\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::GdcProtocolClient gdc(components[COMPONENT_GDC]);
  if (!gdc.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_GDC not available\n", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::IspProtocolClient isp(components[COMPONENT_ISP]);
  if (!isp.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_ISP not available\n", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  auto controller =
      std::make_unique<Controller>(parent, components[COMPONENT_ISP], components[COMPONENT_GDC]);

  auto status = controller->StartThread();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Could not start loop thread\n", __func__);
    return status;
  }
  *out = std::move(controller);
  return ZX_OK;
}

zx_status_t ControllerBind(void* ctx, zx_device_t* device) {
  std::unique_ptr<Controller> controller;
  auto status = camera::Controller::Setup(device, &controller);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "%s: Could not setup camera_controller: %d\n", __func__, status);
    return status;
  }

  status = controller->DdkAdd("camera-controller");
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "%s: Could not add camera_controller device: %d\n", __func__, status);
    return status;
  }

  FX_LOGF(INFO, "", "%s: camera_controller driver added\n", __func__);

  // controller device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto* dev = controller.release();
  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ControllerBind;
  return ops;
}();

}  // namespace camera

// clang-format off
ZIRCON_DRIVER_BEGIN(camera_controller, camera::driver_ops, "camera_ctrl", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_CAMERA_CONTROLLER),
ZIRCON_DRIVER_END(camera_controller)
