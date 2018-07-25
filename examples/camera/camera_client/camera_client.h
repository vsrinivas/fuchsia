// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_CAMERA_CAMERA_CLIENT_CAMERA_CLIENT_H_
#define GARNET_EXAMPLES_CAMERA_CAMERA_CLIENT_CAMERA_CLIENT_H_

#include <garnet/drivers/usb_video/usb-video-camera.h>
#include "lib/component/cpp/startup_context.h"

namespace camera {

class Client {
 public:
  Client();

  explicit Client(std::unique_ptr<component::StartupContext> context);

  fuchsia::camera::driver::ControlSyncPtr& camera();

  zx_status_t Open(int dev_id);

 private:
  fuchsia::camera::driver::ControlSyncPtr camera_control_;
  std::unique_ptr<component::StartupContext> context_;
};

}  // namespace camera

#endif  // GARNET_EXAMPLES_CAMERA_CAMERA_CLIENT_CAMERA_CLIENT_H_
