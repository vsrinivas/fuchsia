// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_CAMERA_GYM_CONTROLLER_CLIENT_CONTROLLER_CLIENT_APP_H_
#define SRC_CAMERA_BIN_CAMERA_GYM_CONTROLLER_CLIENT_CONTROLLER_CLIENT_APP_H_

#include <fuchsia/camera/gym/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

namespace camera {

struct Service {
  std::string name;
  std::string service_path;
};

// ControllerClientApp allow the simple exercise of passing arguments to the service side
// (camera-gym-manual). The arguments are passed as-is. Range checks must be done by the service
// side.
class ControllerClientApp {
 public:
  explicit ControllerClientApp(async::Loop& loop);

  // Kick off the passing of commands to the service side.
  void Start(std::vector<fuchsia::camera::gym::Command> commands);

 private:
  ControllerClientApp(const ControllerClientApp&) = delete;
  ControllerClientApp& operator=(const ControllerClientApp&) = delete;

  bool ConnectToServer();
  void SendCommand(std::vector<fuchsia::camera::gym::Command> commands, uint32_t index);
  void Quit();

  async::Loop& loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::camera::gym::ControllerPtr controller_;
  std::vector<fuchsia::camera::gym::Command> commands_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_CAMERA_GYM_CONTROLLER_CLIENT_CONTROLLER_CLIENT_APP_H_
