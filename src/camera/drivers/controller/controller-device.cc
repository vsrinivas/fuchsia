// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller-device.h"

#include <lib/fidl/cpp/binding.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>

#include "src/lib/syslog/cpp/logger.h"

namespace camera {

constexpr auto TAG = "camera_controller";

namespace {
enum {
  COMPONENT_ISP,
  COMPONENT_GDC,
  COMPONENT_SYSMEM,
  COMPONENT_COUNT,
};
}  // namespace

void ControllerDevice::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void ControllerDevice::DdkRelease() { delete this; }

zx_status_t ControllerDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_camera_Device_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t ControllerDevice::GetChannel2(zx_handle_t handle) {
  if (handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (controller_ != nullptr) {
    zx::channel channel(handle);
    zxlogf(ERROR, "%s: Camera2 Controller already running", __func__);
    return ZX_ERR_INTERNAL;
  }

  zx::channel channel(handle);
  fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> control_interface(std::move(channel));

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;

  auto status = sysmem_.Connect(sysmem_allocator.NewRequest().TakeChannel());
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "Could not setup sysmem allocator";
    return status;
  }

  if (control_interface.is_valid()) {
    controller_ = std::make_unique<ControllerImpl>(
        parent(), std::move(control_interface), controller_loop_.dispatcher(), isp_, gdc_,
        [this] { controller_ = nullptr; }, std::move(sysmem_allocator));

    return ZX_OK;
  }
  return ZX_ERR_INTERNAL;
}

void ControllerDevice::ShutDown() { controller_loop_.Shutdown(); }

zx_status_t ControllerDevice::StartThread() {
  return controller_loop_.StartThread("camera-controller-loop", &loop_thread_);
}

// static
zx_status_t ControllerDevice::Setup(zx_device_t* parent, std::unique_ptr<ControllerDevice>* out) {
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

  ddk::SysmemProtocolClient sysmem(components[COMPONENT_SYSMEM]);
  if (!sysmem.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_SYSMEM not available\n", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  auto controller = std::make_unique<ControllerDevice>(
      parent, components[COMPONENT_ISP], components[COMPONENT_GDC], components[COMPONENT_SYSMEM]);

  auto status = controller->StartThread();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Could not start loop thread\n", __func__);
    return status;
  }
  *out = std::move(controller);
  return ZX_OK;
}

zx_status_t ControllerDeviceBind(void* /*ctx*/, zx_device_t* device) {
  std::unique_ptr<ControllerDevice> controller_device;
  auto status = camera::ControllerDevice::Setup(device, &controller_device);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "Could not setup camera_controller_device";
    return status;
  }

  status = controller_device->DdkAdd("camera-controller-device");
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "Could not add camera_controller_device device";
    return status;
  }

  FX_PLOGST(INFO, TAG, status) << "camera_controller_device driver added";

  // controller device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto* dev = controller_device.release();
  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ControllerDeviceBind;
  return ops;
}();

}  // namespace camera

// clang-format off
ZIRCON_DRIVER_BEGIN(camera_controller, camera::driver_ops, "camera_ctrl", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_CAMERA_CONTROLLER),
ZIRCON_DRIVER_END(camera_controller)
