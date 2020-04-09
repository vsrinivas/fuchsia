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
                    public fuchsia::ui::input::PointerCaptureListenerRegistry {
 public:
  struct PointerCaptureListener {
    fuchsia::ui::input::PointerCaptureListenerPtr listener_ptr;
    fuchsia::ui::views::ViewRef view_ref;
  };

  static constexpr TypeId kTypeId = kInput;
  static const char* kName;

  explicit InputSystem(SystemContext context, fxl::WeakPtr<gfx::SceneGraph> scene_graph);
  ~InputSystem() override = default;

  CommandDispatcherUniquePtr CreateCommandDispatcher(
      scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
      std::shared_ptr<ErrorReporter> error_reporter) override;

  fuchsia::ui::input::ImeServicePtr& ime_service() { return ime_service_; }

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

  // |fuchsia.ui.pointercapture.ListenerRegistry|
  void RegisterListener(
      fidl::InterfaceHandle<fuchsia::ui::input::PointerCaptureListener> listener_handle,
      fuchsia::ui::views::ViewRef view_ref, RegisterListenerCallback success_callback) override;

  void DispatchPointerCommand(const fuchsia::ui::input::SendPointerInputCmd& command,
                              scheduling::SessionId session_id, bool parallel_dispatch);

  // Retrieve focused ViewRef's KOID from the scene graph.
  // Return ZX_KOID_INVALID if scene does not exist, or if the focus chain is empty.
  zx_koid_t focus() const;

 private:
  // Gets the global transform of the view corresponding to |view_ref| from the scene graph's view
  // tree. Return std::nullopt for exceptional cases (e.g., invalid or unknown view ref).
  std::optional<glm::mat4> GetGlobalTransformByViewRef(
      const fuchsia::ui::views::ViewRef& view_ref) const;

  void DispatchTouchCommand(const fuchsia::ui::input::SendPointerInputCmd& command,
                            const gfx::LayerStackPtr& layer_stack, scheduling::SessionId session_id,
                            bool parellel_dispatch, bool a11y_enabled);
  void DispatchMouseCommand(const fuchsia::ui::input::SendPointerInputCmd& command,
                            const gfx::LayerStackPtr& layer_stack);

  // Dispatches an event to a parallel set of views; set may be empty.
  // Conditionally trigger focus change request, based on |views_and_event.event.phase|.
  // Called by PointerEventBuffer.
  void DispatchDeferredPointerEvent(PointerEventBuffer::DeferredPointerEvent views_and_event);

  // Enqueue the pointer event into the entry in a ViewStack.
  static void ReportPointerEvent(const ViewStack::Entry& view_info,
                                 const fuchsia::ui::input::PointerEvent& pointer);

  // Retrieve KOID of focus chain's root view.
  // Return ZX_KOID_INVALID if scene does not exist, or if the focus chain is empty.
  zx_koid_t focus_chain_root() const;

  // Request a focus change in the SceneGraph's ViewTree.
  //
  // The request is performed with the authority of the focus chain's root view (typically the
  // Scene). However, a request may be denied if the requested view may not receive focus (a
  // property set by the view holder).
  void RequestFocusChange(zx_koid_t view_ref_koid);

  // Checks if an accessibility listener is intercepting pointer events. If the
  // listener is on, initializes the buffer if it hasn't been created.
  // Important:
  // When the buffer is initialized, it can be the case that there are active
  // pointer event streams that haven't finished yet. They are sent to clients,
  // and *not* to the a11y listener. When the stream is done and a new stream
  // arrives, these will be sent to the a11y listener who will just continue its
  // normal flow. In a disconnection, if there are active pointer event streams,
  // its assume that the listener rejected them so they are sent to clients.
  bool ShouldForwardAccessibilityPointerEvents();

  // Send a copy of the event to the singleton listener of the pointer capture API if there is one.
  // TODO(48150): Delete when we delete the PointerCapture functionality.
  void ReportPointerEventToPointerCaptureListener(const fuchsia::ui::input::PointerEvent& pointer,
                                                  GlobalId compositor_id) const;

  fxl::WeakPtr<gfx::SceneGraph> scene_graph_;

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

  // When accessibility pointer event forwarding is enabled, this buffer stores
  // pointer events until an accessibility listener decides how to handle them.
  // It is always null otherwise.
  std::unique_ptr<PointerEventBuffer> pointer_event_buffer_;

  fidl::BindingSet<fuchsia::ui::input::PointerCaptureListenerRegistry> pointer_capture_registry_;
  // A singleton listener who wants to be notified when pointer events happen.
  // We honor the first pointer capture listener to register. A call to RegisterListener()
  // above will fail if there is already a registered listener.
  std::optional<PointerCaptureListener> pointer_capture_listener_;

  // Tracks the set of Views each touch event is delivered to; basically, a map from pointer ID to a
  // stack of ViewRef KOIDs. This is used to ensure consistent delivery of pointer events for a
  // given finger to its original destination targets on their respective DOWN event.  In
  // particular, a focus change triggered by a new finger should *not* affect delivery of events to
  // existing fingers.
  //
  // NOTE: We assume there is one touch screen, and hence unique pointer IDs.
  std::unordered_map<uint32_t, ViewStack> touch_targets_;

  // Tracks the View each mouse pointer is delivered to; a map from device ID to a ViewRef KOID.
  // This is used to ensure consistent delivery of mouse events for a given device.  A focus change
  // triggered by other pointer events should *not* affect delivery of events to existing mice.
  //
  // NOTE: We reuse the ViewStack here just for convenience.
  std::unordered_map<uint32_t, ViewStack> mouse_targets_;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_INPUT_SYSTEM_H_
