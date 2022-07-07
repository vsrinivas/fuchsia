// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_
#define SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <functional>

#include "src/ui/scenic/lib/scenic/command_dispatcher.h"

namespace scenic_impl::input {

// Legacy API implementation.
// Per-session treatment of input commands.
// Routes input events to Scenic clients.
class InputCommandDispatcher : public CommandDispatcher {
 public:
  InputCommandDispatcher(
      scheduling::SessionId session_id,
      std::function<void(fuchsia::ui::input::SendPointerInputCmd, scheduling::SessionId)>
          dispatch_pointer_command);
  ~InputCommandDispatcher() override = default;

  // |CommandDispatcher|
  void SetDebugName(const std::string& debug_name) override {}

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command,
                       scheduling::PresentId present_id) override;

 private:
  const scheduling::SessionId session_id_;
  const std::function<void(fuchsia::ui::input::SendPointerInputCmd, scheduling::SessionId)>
      dispatch_pointer_command_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_
