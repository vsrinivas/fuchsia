// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_SENSOR_CLI_DEBUG_CLIENT_H_
#define SRC_CAMERA_BIN_SENSOR_CLI_DEBUG_CLIENT_H_

#include <fuchsia/camera2/debug/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>

namespace camera {

struct Service {
  std::string name;
  std::string service_path;
};

// DebugClient allow the simple exercise of passing arguments to the service side (camera
// controller). The arguments are passed as-is. Range checks must be done by the service side.
class DebugClient {
 public:
  explicit DebugClient(async::Loop& loop);

  void Start(uint32_t mode);

 private:
  DebugClient(const DebugClient&) = delete;
  DebugClient& operator=(const DebugClient&) = delete;

  void Execute();
  bool ConnectToServer();
  void SendCommand(uint32_t mode);
  void Quit();

  async::Loop& loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::hardware::camera::DeviceSyncPtr device_sync_ptr_;
  fuchsia::camera2::debug::DebugPtr debug_ptr_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_SENSOR_CLI_DEBUG_CLIENT_H_
