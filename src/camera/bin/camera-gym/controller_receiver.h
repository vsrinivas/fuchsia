// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_CAMERA_GYM_CONTROLLER_RECEIVER_H_
#define SRC_CAMERA_BIN_CAMERA_GYM_CONTROLLER_RECEIVER_H_

#include <fuchsia/camera/gym/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fpromise/result.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/types.h>  // zx_status_t

namespace camera {

// The primary responsibilities of ControllerReceiver are:
//
// 1. Receive Command from the command line front end.
// 2. Dispatch Command to StreamCycler.
class ControllerReceiver : public fuchsia::camera::gym::Controller {
 public:
  ControllerReceiver();

  fidl::InterfaceRequestHandler<fuchsia::camera::gym::Controller> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // The command execution
  using CommandHandler = fit::function<void(
      fuchsia::camera::gym::Command, camera::ControllerReceiver::SendCommandCallback callback)>;

  void SetHandlers(CommandHandler command_handler);

 private:
  ControllerReceiver(const ControllerReceiver&) = delete;
  ControllerReceiver& operator=(const ControllerReceiver&) = delete;

  void SendCommand(fuchsia::camera::gym::Command command, SendCommandCallback callback) override;

  fidl::BindingSet<fuchsia::camera::gym::Controller> bindings_;

  CommandHandler command_handler_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_CAMERA_GYM_CONTROLLER_RECEIVER_H_
