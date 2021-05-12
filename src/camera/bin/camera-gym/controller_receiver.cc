// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller_receiver.h"

#include <fuchsia/camera/gym/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

namespace camera {

using Command = fuchsia::camera::gym::Command;

ControllerReceiver::ControllerReceiver() = default;

void ControllerReceiver::SetHandlers(CommandHandler command_handler) {
  command_handler_ = std::move(command_handler);
}

void ControllerReceiver::SendCommand(Command command, SendCommandCallback callback) {
  ZX_ASSERT(command_handler_);
  command_handler_(std::move(command), std::move(callback));
}

}  // namespace camera
