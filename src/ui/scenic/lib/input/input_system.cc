// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_system.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <zircon/status.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/input/helper.h"

namespace scenic_impl {
namespace input {

const char* InputSystem::kName = "InputSystem";

InputSystem::InputSystem(SystemContext context, fxl::WeakPtr<gfx::SceneGraph> scene_graph)
    : System(std::move(context)), scene_graph_(scene_graph) {
  FXL_CHECK(scene_graph);
  ime_service_ = this->context()->app_context()->svc()->Connect<fuchsia::ui::input::ImeService>();
  ime_service_.set_error_handler(
      [](zx_status_t status) { FXL_LOG(ERROR) << "Scenic lost connection to TextSync"; });

  this->context()->app_context()->outgoing()->AddPublicService(
      accessibility_pointer_event_registry_.GetHandler(this));

  this->context()->app_context()->outgoing()->AddPublicService(
      pointer_capture_registry_.GetHandler(this));

  FXL_LOG(INFO) << "Scenic input system initialized.";
}

CommandDispatcherUniquePtr InputSystem::CreateCommandDispatcher(
    scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
    std::shared_ptr<ErrorReporter> error_reporter) {
  return CommandDispatcherUniquePtr(
      new InputCommandDispatcher(session_id, std::move(event_reporter), scene_graph_, this),
      // Custom deleter.
      [](CommandDispatcher* cd) { delete cd; });
}

void InputSystem::Register(
    fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
        pointer_event_listener,
    RegisterCallback callback) {
  if (!accessibility_pointer_event_listener_) {
    accessibility_pointer_event_listener_.Bind(std::move(pointer_event_listener));
    callback(/*success=*/true);
  } else {
    // An accessibility listener is already registered.
    callback(/*success=*/false);
  }
}

std::optional<glm::mat4> InputSystem::GetGlobalTransformByViewRef(
    const fuchsia::ui::views::ViewRef& view_ref) const {
  if (!scene_graph_) {
    return std::nullopt;
  }
  zx_koid_t view_ref_koid = fsl::GetKoid(view_ref.reference.get());
  return scene_graph_->view_tree().GlobalTransformOf(view_ref_koid);
}

void InputSystem::RegisterListener(
    fidl::InterfaceHandle<fuchsia::ui::input::PointerCaptureListener> listener_handle,
    fuchsia::ui::views::ViewRef view_ref, RegisterListenerCallback success_callback) {
  if (pointer_capture_listener_) {
    // Already have a listener, decline registration.
    success_callback(false);
    return;
  }

  fuchsia::ui::input::PointerCaptureListenerPtr new_listener;
  new_listener.Bind(std::move(listener_handle));

  // Remove listener if the interface closes.
  new_listener.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "Pointer capture listener interface closed with error: "
                   << zx_status_get_string(status);
    pointer_capture_listener_ = std::nullopt;
  });

  pointer_capture_listener_ = PointerCaptureListener{.listener_ptr = std::move(new_listener),
                                                     .view_ref = std::move(view_ref)};

  success_callback(true);
}

void InputSystem::ReportPointerEventToPointerCaptureListener(
    const fuchsia::ui::input::PointerEvent& pointer,
    const glm::mat4& screen_to_world_transform) const {
  if (!pointer_capture_listener_) {
    return;
  }

  const PointerCaptureListener& listener = pointer_capture_listener_.value();

  std::optional<glm::mat4> view_to_world_transform = GetGlobalTransformByViewRef(listener.view_ref);
  if (!view_to_world_transform) {
    return;
  }

  const auto world_to_view_transform = glm::inverse(view_to_world_transform.value());
  const auto screen_to_view_transform = world_to_view_transform * screen_to_world_transform;
  const auto local_coords =
      TransformPointerCoords(PointerCoords(pointer), screen_to_view_transform);
  const auto local_pointer = ClonePointerWithCoords(pointer, local_coords);

  // TODO(42145): Implement flow control.
  listener.listener_ptr->OnPointerEvent(std::move(local_pointer), [] {});
}

}  // namespace input
}  // namespace scenic_impl
