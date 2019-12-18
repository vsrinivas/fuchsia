// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INPUT_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_INPUT_INPUT_SYSTEM_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/accessibility/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <map>
#include <optional>

#include "src/ui/scenic/lib/input/input_command_dispatcher.h"
#include "src/ui/scenic/lib/scenic/system.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace scenic_impl {
namespace input {

// Tracks input APIs.
class InputSystem : public System,
                    public fuchsia::ui::policy::accessibility::PointerEventRegistry,
                    public fuchsia::ui::scenic::PointerCaptureListenerRegistry {
 public:
  struct PointerCaptureListener {
    fuchsia::ui::scenic::PointerCaptureListenerPtr listener_ptr;
    fuchsia::ui::views::ViewRef view_ref;
  };

  static constexpr TypeId kTypeId = kInput;
  static const char* kName;

  explicit InputSystem(SystemContext context, gfx::Engine* engine);
  ~InputSystem() override = default;

  CommandDispatcherUniquePtr CreateCommandDispatcher(CommandDispatcherContext context) override;

  fuchsia::ui::input::ImeServicePtr& ime_service() { return ime_service_; }

  fuchsia::ui::input::accessibility::PointerEventListenerPtr&
  accessibility_pointer_event_listener() {
    return accessibility_pointer_event_listener_;
  }

  bool IsAccessibilityPointerEventForwardingEnabled() const {
    return accessibility_pointer_event_listener_ &&
           accessibility_pointer_event_listener_.is_bound();
  }

  std::map<scheduling::SessionId, EventReporterWeakPtr>& hard_keyboard_requested() {
    return hard_keyboard_requested_;
  }

  // |fuchsia.ui.policy.accessibility.PointerEventRegistry|
  void Register(fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
                    pointer_event_listener,
                RegisterCallback callback) override;

  // Gets the global transform of the view corresponding to |view_ref| from the scene graph's view
  // tree. Return std::nullopt for exceptional cases (e.g., invalid or unknown view ref).
  std::optional<glm::mat4> GetGlobalTransformByViewRef(
      const fuchsia::ui::views::ViewRef& view_ref) const;

  // |fuchsia.ui.pointercapture.ListenerRegistry|
  void RegisterListener(
      fidl::InterfaceHandle<fuchsia::ui::scenic::PointerCaptureListener> listener_handle,
      fuchsia::ui::views::ViewRef view_ref, RegisterListenerCallback success_callback) override;

  // Send a copy of the event to the singleton listener of the pointer capture API if there is one.
  void ReportPointerEventToPointerCaptureListener(
      const fuchsia::ui::input::PointerEvent& pointer) const;

 private:
  gfx::Engine* const engine_;

  // Send hard keyboard events to IME Service for dispatch via IME.
  // NOTE: This flow will be replaced by a direct dispatch from a "Root Presenter" to IME Service.
  fuchsia::ui::input::ImeServicePtr ime_service_;

  // By default, clients don't get hard keyboard events directly from Scenic.
  // Clients may request these events via the SetHardKeyboardDeliveryCmd;
  // this set remembers which sessions have opted in.  We need this map because
  // each InputCommandDispatcher works independently.
  // NOTE: This flow will be replaced by a direct dispatch from a "Root Presenter" to IME Service.
  std::map<scheduling::SessionId, EventReporterWeakPtr> hard_keyboard_requested_;

  fidl::BindingSet<fuchsia::ui::policy::accessibility::PointerEventRegistry>
      accessibility_pointer_event_registry_;
  // We honor the first accessibility listener to register. A call to Register()
  // above will fail if there is already a registered listener.
  fuchsia::ui::input::accessibility::PointerEventListenerPtr accessibility_pointer_event_listener_;

  fidl::BindingSet<fuchsia::ui::scenic::PointerCaptureListenerRegistry> pointer_capture_registry_;
  // A singleton listener who wants to be notified when pointer events happen.
  // We honor the first pointer capture listener to register. A call to RegisterListener()
  // above will fail if there is already a registered listener.
  std::optional<PointerCaptureListener> pointer_capture_listener_;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_INPUT_SYSTEM_H_
