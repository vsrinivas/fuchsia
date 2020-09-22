// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_
#define SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/accessibility/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <functional>
#include <memory>
#include <unordered_map>

#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/input/pointer_event_buffer.h"

namespace scenic_impl {
namespace input {

class InputSystem;

// Per-session treatment of input commands.
// Routes input events from a root presenter to Scenic clients.
// Manages input-related state, such as focus.
//
// The general flow of events is:
// If accessibility is off:
// DispatchCommand --[decide what/where]--> EnqueueEvent
// If accessibility is on:
// DispatchCommand --> accessibility --[does accessibility want to block it? then stop]--[otherwise
// decide where else to send]--> EnqueueEvent
class InputCommandDispatcher : public CommandDispatcher {
 public:
  InputCommandDispatcher(scheduling::SessionId session_id,
                         std::shared_ptr<EventReporter> event_reporter,
                         fxl::WeakPtr<gfx::SceneGraph> scene_graph, InputSystem* input_system);
  ~InputCommandDispatcher() override = default;

  // |CommandDispatcher|
  void SetDebugName(const std::string& debug_name) override {}

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command,
                       scheduling::PresentId present_id) override;

 private:
  // Per-command dispatch logic.
  void DispatchCommand(const fuchsia::ui::input::SendKeyboardInputCmd& command);
  void DispatchCommand(const fuchsia::ui::input::SetHardKeyboardDeliveryCmd& command);
  void DispatchCommand(const fuchsia::ui::input::SetParallelDispatchCmd& command);

  // Enqueue the keyboard event into an EventReporter.
  static void ReportKeyboardEvent(EventReporter* reporter,
                                  fuchsia::ui::input::KeyboardEvent keyboard);

  // Enqueue the keyboard event to the IME Service.
  static void ReportToImeService(const fuchsia::ui::input::ImeServicePtr& ime_service,
                                 fuchsia::ui::input::KeyboardEvent keyboard);

  // FIELDS
  const scheduling::SessionId session_id_;
  std::shared_ptr<EventReporter> event_reporter_;
  fxl::WeakPtr<gfx::SceneGraph> scene_graph_;
  InputSystem* const input_system_ = nullptr;

  // TODO(fxbug.dev/24258): Remove when gesture disambiguation is the default.
  bool parallel_dispatch_ = true;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_
