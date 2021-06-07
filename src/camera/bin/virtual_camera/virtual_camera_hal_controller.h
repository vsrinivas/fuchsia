// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_VIRTUAL_CAMERA_VIRTUAL_CAMERA_HAL_CONTROLLER_H_
#define SRC_CAMERA_BIN_VIRTUAL_CAMERA_VIRTUAL_CAMERA_HAL_CONTROLLER_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include "src/camera/bin/virtual_camera/stream_storage.h"

namespace camera {

// Implementation of |fuchsia::camera2::hal::Controller| which binds to a single
// client.
class VirtualCameraHalController : public fuchsia::camera2::hal::Controller {
 public:
  VirtualCameraHalController(StreamStorage& stream_storage,
                             fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> request);

  // fuchsia::camera2::hal::Controller
  void GetNextConfig(fuchsia::camera2::hal::Controller::GetNextConfigCallback callback) override;
  void CreateStream(uint32_t config_index, uint32_t stream_index, uint32_t image_format_index,
                    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) override;
  void EnableStreaming() override;
  void DisableStreaming() override;
  void GetDeviceInfo(fuchsia::camera2::hal::Controller::GetDeviceInfoCallback callback) override;

 private:
  StreamStorage& stream_storage_;
  fidl::Binding<fuchsia::camera2::hal::Controller> binding_;

  // This controller returns a single |fuchsia::camera2::hal::Config| when
  // |GetNextConfig| is called. This is used to determine if |GetNextConfig|
  // should return ZX_ERR_STOP if a config has already been returned.
  bool get_next_config_called_ = false;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_VIRTUAL_CAMERA_VIRTUAL_CAMERA_HAL_CONTROLLER_H_
