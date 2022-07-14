// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_system.h"

#include "src/ui/scenic/lib/input/input_command_dispatcher.h"

namespace scenic_impl::input {

const char* InputSystem::kName = "InputSystem";

InputSystem::InputSystem(SystemContext context, fxl::WeakPtr<gfx::SceneGraph> scene_graph,
                         fit::function<void(zx_koid_t)> request_focus)
    : System(std::move(context)),
      request_focus_(std::move(request_focus)),
      hit_tester_(view_tree_snapshot_, *System::context()->inspect_node()),
      mouse_system_(System::context()->app_context(), view_tree_snapshot_, hit_tester_,
                    [this](zx_koid_t koid) { request_focus_(koid); }),
      touch_system_(
          System::context()->app_context(), view_tree_snapshot_, hit_tester_,
          *System::context()->inspect_node(), [this](zx_koid_t koid) { request_focus_(koid); },
          std::move(scene_graph)),
      pointerinjector_registry_(
          this->context()->app_context(),
          /*inject_touch_exclusive=*/
          [&touch_system = touch_system_](const InternalTouchEvent& event, StreamId stream_id) {
            touch_system.InjectTouchEventExclusive(event, stream_id);
          },
          /*inject_touch_hit_tested=*/
          [&touch_system = touch_system_](const InternalTouchEvent& event, StreamId stream_id) {
            touch_system.InjectTouchEventHitTested(event, stream_id);
          },
          /*inject_mouse_exclusive=*/
          [&mouse_system = mouse_system_](const InternalMouseEvent& event, StreamId stream_id) {
            mouse_system.InjectMouseEventExclusive(event, stream_id);
          },
          /*inject_mouse_hit_tested=*/
          [&mouse_system = mouse_system_](const InternalMouseEvent& event, StreamId stream_id) {
            mouse_system.InjectMouseEventHitTested(event, stream_id);
          },
          // Explicit call necessary to cancel mouse stream, because mouse stream itself does not
          // track phase.
          /*cancel_mouse_stream=*/
          [&mouse_system = mouse_system_](StreamId stream_id) {
            mouse_system.CancelMouseStream(stream_id);
          },
          System::context()->inspect_node()->CreateChild("PointerinjectorRegistry")) {}

CommandDispatcherUniquePtr InputSystem::CreateCommandDispatcher(
    scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
    std::shared_ptr<ErrorReporter> error_reporter) {
  return CommandDispatcherUniquePtr(
      new InputCommandDispatcher(session_id,
                                 [&touch_system = touch_system_](auto command, auto session_id) {
                                   touch_system.DispatchPointerCommand(std::move(command),
                                                                       session_id);
                                 }),
      // Custom deleter.
      [](CommandDispatcher* cd) { delete cd; });
}

}  // namespace scenic_impl::input
