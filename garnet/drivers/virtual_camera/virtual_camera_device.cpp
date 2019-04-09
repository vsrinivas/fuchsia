// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/virtual_camera/virtual_camera_device.h"

#include "garnet/drivers/virtual_camera/virtual_camera_control.h"
#include "src/lib/fxl/logging.h"

namespace virtual_camera {

std::unique_ptr<async::Loop> VirtualCameraDevice::fidl_dispatch_loop_ = nullptr;

#define DEV(c) static_cast<VirtualCameraDevice*>(c)
static zx_protocol_device_t virtual_camera_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .message = [](void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) -> zx_status_t {
      return DEV(ctx)->Message(msg, txn);
    },
};
#undef DEV

VirtualCameraDevice::VirtualCameraDevice() {
  if (fidl_dispatch_loop_ == nullptr) {
    fidl_dispatch_loop_ =
        std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
    fidl_dispatch_loop_->StartThread();
  }
}

VirtualCameraDevice::~VirtualCameraDevice() {}

zx_status_t VirtualCameraDevice::Bind(zx_device_t* device) {
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "virtual_camera";
  args.ctx = this;
  args.ops = &virtual_camera_device_ops;

  // Add the virtual_audio device node, under parent /dev/test.
  return device_add(device, &args, &dev_node_);
}

void VirtualCameraDevice::Unbind() {
  // Unpublish our device node.
  // DdkRemove();
}

void VirtualCameraDevice::Release() { delete this; }

zx_status_t VirtualCameraDevice::Message(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_camera_Device_dispatch(this, txn, msg,
                                                 &CAMERA_FIDL_THUNKS);
}

zx_status_t VirtualCameraDevice::GetChannel(zx_handle_t handle) {
  if (handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_INVALID_ARGS;
  }

  // CameraStream FIDL interface
  static std::unique_ptr<VirtualCameraControlImpl>
      virtual_camera_camera_control_server_ = nullptr;

  if (virtual_camera_camera_control_server_ != nullptr) {
    FXL_LOG(ERROR) << "Camera Control already running";
    // TODO(CAM-XXX): support multiple concurrent clients.
    return ZX_ERR_INTERNAL;
  }

  zx::channel channel(handle);
  fidl::InterfaceRequest<fuchsia::camera::Control> control_interface(
      std::move(channel));

  if (control_interface.is_valid()) {
    virtual_camera_camera_control_server_ =
        std::make_unique<VirtualCameraControlImpl>(
            std::move(control_interface), fidl_dispatch_loop_->dispatcher(),
            [] { virtual_camera_camera_control_server_.reset(); });

    return ZX_OK;
  } else {
    return ZX_ERR_INTERNAL;
  }
}

const fuchsia_hardware_camera_Device_ops_t
    VirtualCameraDevice::CAMERA_FIDL_THUNKS{
        .GetChannel = [](void* ctx, zx_handle_t handle) -> zx_status_t {
          return reinterpret_cast<VirtualCameraDevice*>(ctx)->GetChannel(
              handle);
        },
    };

}  // namespace virtual_camera
