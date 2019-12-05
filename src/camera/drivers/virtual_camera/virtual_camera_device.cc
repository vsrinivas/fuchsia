// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/virtual_camera/virtual_camera_device.h"

#include "src/camera/drivers/virtual_camera/virtual_camera2_control.h"
#include "src/camera/drivers/virtual_camera/virtual_camera_control.h"
#include "src/lib/syslog/cpp/logger.h"

namespace camera {

constexpr auto TAG = "virtual_camera";

std::unique_ptr<async::Loop> VirtualCameraDevice::fidl_dispatch_loop_ = nullptr;

static zx_protocol_device_t virtual_camera_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { static_cast<VirtualCameraDevice*>(ctx)->Unbind(); },
    .release = [](void* ctx) { static_cast<VirtualCameraDevice*>(ctx)->Release(); },
    .message = [](void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) -> zx_status_t {
      return static_cast<VirtualCameraDevice*>(ctx)->Message(msg, txn);
    },
};

VirtualCameraDevice::VirtualCameraDevice() {
  if (fidl_dispatch_loop_ == nullptr) {
    fidl_dispatch_loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    fidl_dispatch_loop_->StartThread();
  }
}

zx_status_t VirtualCameraDevice::Bind(zx_device_t* device) {
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "virtual_camera";
  args.ctx = this;
  args.ops = &virtual_camera_device_ops;
  args.proto_id = ZX_PROTOCOL_CAMERA;

  // Add the virtual_audio device node, under parent /dev/test.
  return device_add(device, &args, &dev_node_);
}

void VirtualCameraDevice::Unbind() {
  // Unpublish our device node.
  // DdkRemoveDeprecated();
}

void VirtualCameraDevice::Release() { delete this; }

zx_status_t VirtualCameraDevice::Message(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_camera_Device_dispatch(this, txn, msg, &CAMERA_FIDL_THUNKS);
}

zx_status_t VirtualCameraDevice::GetChannel(zx_handle_t handle) {
  if (handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_INVALID_ARGS;
  }

  // CameraStream FIDL interface
  static std::unique_ptr<VirtualCameraControlImpl> server_ = nullptr;

  if (server_ != nullptr) {
    FX_LOGST(ERROR, TAG) << "Camera Control already running";
    return ZX_ERR_INTERNAL;
  }

  zx::channel channel(handle);
  fidl::InterfaceRequest<fuchsia::camera::Control> control_interface(std::move(channel));

  if (control_interface.is_valid()) {
    server_ = std::make_unique<VirtualCameraControlImpl>(
        std::move(control_interface), fidl_dispatch_loop_->dispatcher(), [] { server_ = nullptr; });

    return ZX_OK;
  }
  return ZX_ERR_INTERNAL;
}

zx_status_t VirtualCameraDevice::GetChannel2(zx_handle_t handle) {
  if (handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_INVALID_ARGS;
  }

  // CameraStream FIDL interface
  static std::unique_ptr<VirtualCamera2ControllerImpl> server_ = nullptr;
  if (server_ != nullptr) {
    FX_LOGST(ERROR, TAG) << "Camera2 Controller already running";
    return ZX_ERR_INTERNAL;
  }

  zx::channel channel(handle);
  fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> control_interface(std::move(channel));

  if (control_interface.is_valid()) {
    server_ = std::make_unique<VirtualCamera2ControllerImpl>(
        std::move(control_interface), fidl_dispatch_loop_->dispatcher(), [] { server_ = nullptr; });
    return ZX_OK;
  }
  return ZX_ERR_INTERNAL;
}

const fuchsia_hardware_camera_Device_ops_t VirtualCameraDevice::CAMERA_FIDL_THUNKS{
    .GetChannel = [](void* ctx, zx_handle_t handle) -> zx_status_t {
      return reinterpret_cast<VirtualCameraDevice*>(ctx)->GetChannel(handle);
    },
    .GetChannel2 = [](void* ctx, zx_handle_t handle) -> zx_status_t {
      return reinterpret_cast<VirtualCameraDevice*>(ctx)->GetChannel2(handle);
    },
};

}  // namespace camera
