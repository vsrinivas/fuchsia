// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_command_dispatcher.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/ui/scenic/lib/input/input_system.h"

namespace scenic_impl {
namespace input {

using InputCommand = fuchsia::ui::input::Command;
using ScenicCommand = fuchsia::ui::scenic::Command;

InputCommandDispatcher::InputCommandDispatcher(scheduling::SessionId session_id,
                                               InputSystem* input_system)
    : session_id_(session_id), input_system_(input_system) {
  FX_CHECK(input_system_);
}

void InputCommandDispatcher::DispatchCommand(ScenicCommand command,
                                             scheduling::PresentId present_id) {
  TRACE_DURATION("input", "dispatch_command", "command", "ScenicCmd");
  FX_DCHECK(command.Which() == ScenicCommand::Tag::kInput);

  InputCommand& input = command.input();
  if (input.is_send_pointer_input()) {
    input_system_->DispatchPointerCommand(input.send_pointer_input(), session_id_);
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

}  // namespace input
}  // namespace scenic_impl
