// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/input_system.h"

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <zircon/status.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/input/helper.h"

namespace scenic_impl {
namespace input {

const char* InputSystem::kName = "InputSystem";

InputSystem::InputSystem(SystemContext context, gfx::Engine* engine)
    : System(std::move(context)), engine_(engine) {
  FXL_CHECK(engine_);
  ime_service_ = this->context()->app_context()->svc()->Connect<fuchsia::ui::input::ImeService>();
  ime_service_.set_error_handler(
      [](zx_status_t status) { FXL_LOG(ERROR) << "Scenic lost connection to TextSync"; });

  this->context()->app_context()->outgoing()->AddPublicService(
      accessibility_pointer_event_registry_.GetHandler(this));

  this->context()->app_context()->outgoing()->AddPublicService(
      pointer_capture_registry_.GetHandler(this));

  FXL_LOG(INFO) << "Scenic input system initialized.";
}

CommandDispatcherUniquePtr InputSystem::CreateCommandDispatcher(CommandDispatcherContext context) {
  return CommandDispatcherUniquePtr(new InputCommandDispatcher(std::move(context), engine_, this),
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
  if (!engine_->scene_graph()) {
    return std::nullopt;
  }
  zx_koid_t view_ref_koid = fsl::GetKoid(view_ref.reference.get());
  return engine_->scene_graph()->view_tree().GlobalTransformOf(view_ref_koid);
}

void InputSystem::RegisterListener(
    fidl::InterfaceHandle<fuchsia::ui::scenic::PointerCaptureListener> listener_handle,
    fuchsia::ui::views::ViewRef view_ref, RegisterListenerCallback success_callback) {
  if (pointer_capture_listener_) {
    // Already have a listener, decline registration.
    success_callback(false);
    return;
  }

  fuchsia::ui::scenic::PointerCaptureListenerPtr new_listener;
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
    const fuchsia::ui::input::PointerEvent& pointer) const {
  if (!pointer_capture_listener_) {
    return;
  }

  const PointerCaptureListener& listener = pointer_capture_listener_.value();

  std::optional<glm::mat4> global_transform = GetGlobalTransformByViewRef(listener.view_ref);
  if (!global_transform) {
    return;
  }

  // TODO(42145): Implement flow control.
  listener.listener_ptr->OnPointerEvent(BuildLocalPointerEvent(pointer, global_transform.value()),
                                        [] {});
}

}  // namespace input
}  // namespace scenic_impl
