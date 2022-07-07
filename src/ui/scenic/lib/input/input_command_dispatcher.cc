// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_command_dispatcher.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace scenic_impl::input {

using InputCommand = fuchsia::ui::input::Command;
using ScenicCommand = fuchsia::ui::scenic::Command;

InputCommandDispatcher::InputCommandDispatcher(
    scheduling::SessionId session_id,
    std::function<void(fuchsia::ui::input::SendPointerInputCmd, scheduling::SessionId)>
        dispatch_pointer_command)
    : session_id_(session_id), dispatch_pointer_command_(std::move(dispatch_pointer_command)) {}

void InputCommandDispatcher::DispatchCommand(ScenicCommand command,
                                             scheduling::PresentId present_id) {
  TRACE_DURATION("input", "dispatch_command", "command", "ScenicCmd");
  FX_DCHECK(command.Which() == ScenicCommand::Tag::kInput);

  InputCommand& input = command.input();
  if (input.is_send_pointer_input()) {
    dispatch_pointer_command_(input.send_pointer_input(), session_id_);
  } else if (input.is_send_keyboard_input()) {
    FX_LOGS(WARNING) << "SendKeyboardInputCmd deprecated. Command ignored.";
  } else if (input.is_set_hard_keyboard_delivery()) {
    FX_LOGS(WARNING) << "SetHardKeyboardDeliveryCmd deprecated. Command ignored.";
  } else if (input.is_set_parallel_dispatch()) {
    if (input.set_parallel_dispatch().parallel_dispatch) {
      FX_LOGS(WARNING) << "Parallel dispatch request is ignored and disabled.";
    }
  }
}

}  // namespace scenic_impl::input
