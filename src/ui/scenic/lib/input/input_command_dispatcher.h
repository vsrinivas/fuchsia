// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_
#define SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>

#include "src/ui/scenic/lib/scenic/command_dispatcher.h"

namespace scenic_impl {
namespace input {

class InputSystem;

// Legacy API implementation.
// Per-session treatment of input commands.
// Routes input events to Scenic clients.
class InputCommandDispatcher : public CommandDispatcher {
 public:
  InputCommandDispatcher(scheduling::SessionId session_id, InputSystem* input_system);
  ~InputCommandDispatcher() override = default;

  // |CommandDispatcher|
  void SetDebugName(const std::string& debug_name) override {}

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command,
                       scheduling::PresentId present_id) override;

 private:
  const scheduling::SessionId session_id_;
  InputSystem* const input_system_ = nullptr;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_
