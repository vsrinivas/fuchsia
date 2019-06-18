// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_TEST_CAMERA_CLIENT_TEST_CAMERA_CLIENT_TEST_H_
#define SRC_CAMERA_TEST_CAMERA_CLIENT_TEST_CAMERA_CLIENT_TEST_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

namespace camera {

class Client {
 public:
  Client();

  explicit Client(std::unique_ptr<sys::ComponentContext> context);

  fuchsia::camera::ControlSyncPtr& camera();

  fuchsia::camera::ManagerSyncPtr& manager();

  zx_status_t Open(const char* device);

  // use camera manager - open connections, request device info
  zx_status_t StartManager(int device);

  // use camera driver - open connections, request device info
  zx_status_t StartDriver(const char* device);

  zx_status_t LoadVideoFormats(
      fit::function<zx_status_t(
          uint32_t index, std::vector<fuchsia::camera::VideoFormat>* formats,
          uint32_t* total_format_count)>
          get_formats);

  std::vector<fuchsia::camera::VideoFormat>& formats() { return formats_; };

 private:
  std::vector<fuchsia::camera::VideoFormat> formats_;
  fuchsia::camera::ControlSyncPtr camera_control_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::camera::ManagerSyncPtr manager_;
};

}  // namespace camera

#endif  // SRC_CAMERA_TEST_CAMERA_CLIENT_TEST_CAMERA_CLIENT_TEST_H_
