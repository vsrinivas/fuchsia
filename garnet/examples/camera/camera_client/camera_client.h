// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_CAMERA_CAMERA_CLIENT_CAMERA_CLIENT_H_
#define GARNET_EXAMPLES_CAMERA_CAMERA_CLIENT_CAMERA_CLIENT_H_

#include <fuchsia/camera/cpp/fidl.h>
#include "lib/sys/cpp/startup_context.h"

namespace camera {

class Client {
 public:
  Client();

  explicit Client(std::unique_ptr<sys::StartupContext> context);

  fuchsia::camera::ControlSyncPtr& camera();

  fuchsia::camera::ManagerSyncPtr& manager();

  zx_status_t Open(int dev_id);

  // use camera manager - open connections, request device info
  zx_status_t StartManager();

  // use camera driver - open connections, request device info
  zx_status_t StartDriver();

  zx_status_t LoadVideoFormats(
      std::function<zx_status_t(
          uint32_t index, std::vector<fuchsia::camera::VideoFormat>* formats,
          uint32_t* total_format_count)>
          get_formats);

  std::vector<fuchsia::camera::VideoFormat> formats_;

 private:
  fuchsia::camera::ControlSyncPtr camera_control_;
  std::unique_ptr<sys::StartupContext> context_;
  fuchsia::camera::ManagerSyncPtr manager_;
};

}  // namespace camera

#endif  // GARNET_EXAMPLES_CAMERA_CAMERA_CLIENT_CAMERA_CLIENT_H_
