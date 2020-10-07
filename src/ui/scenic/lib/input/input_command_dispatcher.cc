// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_command_dispatcher.h"

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <memory>
#include <vector>

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/scenic/lib/input/helper.h"
#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"

namespace scenic_impl {
namespace input {

using FocusChangeStatus = scenic_impl::gfx::ViewTree::FocusChangeStatus;
using InputCommand = fuchsia::ui::input::Command;
using ScenicCommand = fuchsia::ui::scenic::Command;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::input::SendPointerInputCmd;

InputCommandDispatcher::InputCommandDispatcher(scheduling::SessionId session_id,
                                               std::shared_ptr<EventReporter> event_reporter,
                                               fxl::WeakPtr<gfx::SceneGraph> scene_graph,
                                               InputSystem* input_system)
    : session_id_(session_id),
      event_reporter_(std::move(event_reporter)),
      scene_graph_(scene_graph),
      input_system_(input_system) {
  FX_CHECK(scene_graph_);
  FX_CHECK(input_system_);
}

void InputCommandDispatcher::DispatchCommand(ScenicCommand command,
                                             scheduling::PresentId present_id) {
  TRACE_DURATION("input", "dispatch_command", "command", "ScenicCmd");
  FX_DCHECK(command.Which() == ScenicCommand::Tag::kInput);
  if (!scene_graph_)
    return;  // Scene graph does not exist. Can not dispatch input.

  InputCommand& input = command.input();
  if (input.is_send_keyboard_input()) {
    DispatchCommand(input.send_keyboard_input());
  } else if (input.is_send_pointer_input()) {
    input_system_->DispatchPointerCommand(input.send_pointer_input(), session_id_,
                                          parallel_dispatch_);
  } else if (input.is_set_hard_keyboard_delivery()) {
    DispatchCommand(input.set_hard_keyboard_delivery());
  } else if (input.is_set_parallel_dispatch()) {
    DispatchCommand(input.set_parallel_dispatch());
  }
}

void InputCommandDispatcher::DispatchCommand(
    const fuchsia::ui::input::SendKeyboardInputCmd& command) {
  TRACE_DURATION("input", "dispatch_command", "command", "SendKeyboardInputCmd");
  // Expected (but soon to be deprecated) event flow.
  ReportToImeService(input_system_->ime_service(), command.keyboard_event);

  // Unusual: Clients may have requested direct delivery when focused.
  const zx_koid_t focused_view = input_system_->focus();
  if (focused_view == ZX_KOID_INVALID)
    return;  // No receiver.

  const scenic_impl::gfx::ViewTree& view_tree = scene_graph_->view_tree();
  EventReporterWeakPtr reporter = view_tree.EventReporterOf(focused_view);
  scheduling::SessionId session_id = view_tree.SessionIdOf(focused_view);
  if (reporter && input_system_->hard_keyboard_requested().count(session_id) > 0) {
    ReportKeyboardEvent(reporter.get(), command.keyboard_event);
  }
}

void InputCommandDispatcher::DispatchCommand(
    const fuchsia::ui::input::SetHardKeyboardDeliveryCmd& command) {
  // Can't easily retrieve owning view's ViewRef KOID from just the Session or SessionId.
  FX_VLOGS(2) << "Hard keyboard events, session_id=" << session_id_
              << ", delivery_request=" << (command.delivery_request ? "on" : "off");

  if (command.delivery_request) {
    // Take this opportunity to remove dead sessions.
    std::vector<scheduling::SessionId> dead_sessions;

    for (auto& reporter : input_system_->hard_keyboard_requested()) {
      if (!reporter.second) {
        dead_sessions.push_back(reporter.first);
      }
    }
    for (auto session_ids : dead_sessions) {
      input_system_->hard_keyboard_requested().erase(session_id_);
    }

    // This code assumes one event reporter per session id.
    if (input_system_->hard_keyboard_requested().count(session_id_) != 0) {
      FX_LOGS(ERROR) << "Hard keyboard requested twice by session " << session_id_
                     << ". Second request ignored.";
      return;
    }
    if (event_reporter_)
      input_system_->hard_keyboard_requested().insert({session_id_, event_reporter_->GetWeakPtr()});
  } else {
    input_system_->hard_keyboard_requested().erase(session_id_);
  }
}

void InputCommandDispatcher::DispatchCommand(
    const fuchsia::ui::input::SetParallelDispatchCmd& command) {
  TRACE_DURATION("input", "dispatch_command", "command", "SetParallelDispatchCmd");
  FX_LOGS(INFO) << "Scenic: Parallel dispatch is turned "
                << (command.parallel_dispatch ? "ON" : "OFF");
  parallel_dispatch_ = command.parallel_dispatch;
}

void InputCommandDispatcher::ReportKeyboardEvent(EventReporter* reporter,
                                                 fuchsia::ui::input::KeyboardEvent keyboard) {
  FX_DCHECK(reporter) << "precondition";

  InputEvent event;
  event.set_keyboard(std::move(keyboard));
  reporter->EnqueueEvent(std::move(event));
}

void InputCommandDispatcher::ReportToImeService(
    const fuchsia::ui::input::ImeServicePtr& ime_service,
    fuchsia::ui::input::KeyboardEvent keyboard) {
  TRACE_DURATION("input", "dispatch_event_to_client", "event_type", "ime_keyboard_event");
  if (ime_service && ime_service.is_bound()) {
    InputEvent event;
    event.set_keyboard(std::move(keyboard));

    ime_service->InjectInput(std::move(event));
  }
}

}  // namespace input
}  // namespace scenic_impl
