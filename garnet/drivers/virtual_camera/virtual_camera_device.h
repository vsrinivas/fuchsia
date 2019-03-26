// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIRTUAL_CAMERA_VIRTUAL_CAMERA_DEVICE_H_
#define GARNET_DRIVERS_VIRTUAL_CAMERA_VIRTUAL_CAMERA_DEVICE_H_

#include <ddk/protocol/test.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fuchsia/camera/cpp/fidl.h>
#include <fuchsia/hardware/camera/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>

namespace virtual_camera {

class VirtualCameraDevice {
 public:
  VirtualCameraDevice();
  ~VirtualCameraDevice();

  // DDK device implementation
  void Unbind();

  void Release();

  zx_status_t Message(fidl_msg_t* msg, fidl_txn_t* txn);

  zx_status_t Bind(zx_device_t* device);

  zx_device_t* dev_node() const { return dev_node_; }

 private:
  // Device FIDL implementation
  zx_status_t GetChannel(zx_handle_t handle);

  static const fuchsia_hardware_camera_Device_ops_t CAMERA_FIDL_THUNKS;

  zx_device_t* dev_node_ = nullptr;

  static std::unique_ptr<async::Loop> fidl_dispatch_loop_;
};

}  // namespace virtual_camera

#endif  // GARNET_DRIVERS_VIRTUAL_CAMERA_VIRTUAL_CAMERA_DEVICE_H_
