// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INPUT_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_INPUT_INPUT_SYSTEM_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/policy/accessibility/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <map>
#include <optional>

#include "src/ui/scenic/lib/input/injector.h"
#include "src/ui/scenic/lib/input/input_command_dispatcher.h"
#include "src/ui/scenic/lib/scenic/system.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace scenic_impl {
namespace input {

// Implementation of PointerEventRegistry API.
class A11yPointerEventRegistry : public fuchsia::ui::policy::accessibility::PointerEventRegistry {
 public:
  A11yPointerEventRegistry(SystemContext* context, fit::function<void()> on_register,
                           fit::function<void()> on_disconnect);

  // |fuchsia.ui.policy.accessibility.PointerEventRegistry|
  void Register(fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
                    pointer_event_listener,
                RegisterCallback callback) override;

  fuchsia::ui::input::accessibility::PointerEventListenerPtr&
  accessibility_pointer_event_listener() {
    return accessibility_pointer_event_listener_;
  }

 private:
  fidl::BindingSet<fuchsia::ui::policy::accessibility::PointerEventRegistry>
      accessibility_pointer_event_registry_;
  // We honor the first accessibility listener to register. A call to Register()
  // above will fail if there is already a registered listener.
  fuchsia::ui::input::accessibility::PointerEventListenerPtr accessibility_pointer_event_listener_;

  // Function called when a new listener successfully registers.
  fit::function<void()> on_register_;

  // Function called when an active listener disconnects.
  fit::function<void()> on_disconnect_;
};

// Tracks input APIs.
class InputSystem : public System,
                    public fuchsia::ui::pointerinjector::Registry,
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

  // |fuchsia.ui.pointerinjector.Registry|
  void Register(fuchsia::ui::pointerinjector::Config config,
                fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> injector,
                RegisterCallback callback) override;

  fuchsia::ui::input::ImeServicePtr& ime_service() { return ime_service_; }

  fuchsia::ui::input::accessibility::PointerEventListenerPtr& accessibility_pointer_event_listener()
      const {
    return pointer_event_registry_->accessibility_pointer_event_listener();
  }

  // Checks if an accessibility listener is intercepting pointer events.
  bool IsA11yListenerEnabled() const {
    return accessibility_pointer_event_listener() &&
           accessibility_pointer_event_listener().is_bound();
  }

  bool IsOwnedByRootSession(const gfx::ViewTree& view_tree, zx_koid_t koid) const;

  std::map<scheduling::SessionId, EventReporterWeakPtr>& hard_keyboard_requested() {
    return hard_keyboard_requested_;
  }

  // |fuchsia.ui.pointercapture.ListenerRegistry|
  void RegisterListener(
      fidl::InterfaceHandle<fuchsia::ui::input::PointerCaptureListener> listener_handle,
      fuchsia::ui::views::ViewRef view_ref, RegisterListenerCallback success_callback) override;

  void DispatchPointerCommand(const fuchsia::ui::input::SendPointerInputCmd& command,
                              scheduling::SessionId session_id, bool parallel_dispatch);

  // Retrieve focused ViewRef's KOID from the scene graph.
  // Return ZX_KOID_INVALID if scene does not exist, or if the focus chain is empty.
  zx_koid_t focus() const;

  // For tests.
  void RegisterA11yListener(
      fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener> listener,
      A11yPointerEventRegistry::RegisterCallback callback) {
    pointer_event_registry_->Register(std::move(listener), std::move(callback));
  }

 private:
  // Perform a hit test with |event| in |view_tree| and collect results in |accumulator|.
  void HitTest(const gfx::ViewTree& view_tree, const InternalPointerEvent& event,
               gfx::HitAccumulator<gfx::ViewHit>& accumulator, bool semantic_hit_test) const;

  // Dispatches an event to a parallel set of views; set may be empty.
  // Conditionally trigger focus change request, based on |views_and_event.event.phase|.
  // Called by PointerEventBuffer.
  void DispatchDeferredPointerEvent(PointerEventBuffer::DeferredPointerEvent views_and_event);

  // Injects a touch event directly to the View with koid |target|.
  void InjectTouchEventExclusive(const InternalPointerEvent& event);

  // Injects a touch event by hit testing for appropriate targets.
  void InjectTouchEventHitTested(const InternalPointerEvent& event, StreamId stream_id,
                                 bool parallel_dispatch);
  void InjectMouseEventHitTested(const InternalPointerEvent& event);

  // Retrieve KOID of focus chain's root view.
  // Return ZX_KOID_INVALID if scene does not exist, or if the focus chain is empty.
  zx_koid_t focus_chain_root() const;

  // Request a focus change in the SceneGraph's ViewTree.
  //
  // The request is performed with the authority of the focus chain's root view (typically the
  // Scene). However, a request may be denied if the requested view may not receive focus (a
  // property set by the view holder).
  void RequestFocusChange(zx_koid_t view_ref_koid);

  // Send a copy of the event to the singleton listener of the pointer capture API if there is one.
  // TODO(48150): Delete when we delete the PointerCapture functionality.
  void ReportPointerEventToPointerCaptureListener(const InternalPointerEvent& event,
                                                  const gfx::ViewTree& view_tree) const;

  // Enqueue the pointer event into the EventReporter of a View.
  void ReportPointerEventToView(const InternalPointerEvent& event, zx_koid_t view_ref_koid,
                                fuchsia::ui::input::PointerEventType type,
                                const gfx::ViewTree& view_tree) const;

  using InjectorId = uint64_t;
  InjectorId last_injector_id_ = 0;
  std::map<InjectorId, Injector> injectors_;

  fxl::WeakPtr<gfx::SceneGraph> scene_graph_;

  std::unique_ptr<A11yPointerEventRegistry> pointer_event_registry_;

  // When accessibility pointer event forwarding is enabled, this buffer stores
  // pointer events until an accessibility listener decides how to handle them.
  // It is always null otherwise.
  std::unique_ptr<PointerEventBuffer> pointer_event_buffer_;

  // Send hard keyboard events to IME Service for dispatch via IME.
  // NOTE: This flow will be replaced by a direct dispatch from a "Root Presenter" to IME Service.
  fuchsia::ui::input::ImeServicePtr ime_service_;

  // By default, clients don't get hard keyboard events directly from Scenic.
  // Clients may request these events via the SetHardKeyboardDeliveryCmd;
  // this set remembers which sessions have opted in.  We need this map because
  // each InputCommandDispatcher works independently.
  // NOTE: This flow will be replaced by a direct dispatch from a "Root Presenter" to IME Service.
  std::map<scheduling::SessionId, EventReporterWeakPtr> hard_keyboard_requested_;

  fidl::BindingSet<fuchsia::ui::pointerinjector::Registry> injector_registry_;

  fidl::BindingSet<fuchsia::ui::input::PointerCaptureListenerRegistry> pointer_capture_registry_;
  // A singleton listener who wants to be notified when pointer events happen.
  // We honor the first pointer capture listener to register. A call to RegisterListener()
  // above will fail if there is already a registered listener.
  std::optional<PointerCaptureListener> pointer_capture_listener_;

  // Tracks the set of Views each touch event is delivered to; basically, a map from pointer ID to a
  // stack of ViewRef KOIDs. This is used to ensure consistent delivery of pointer events for a
  // given finger to its original destination targets on their respective DOWN event. In
  // particular, a focus change triggered by a new finger should *not* affect delivery of events to
  // existing fingers.
  std::unordered_map<uint32_t, std::vector</*view_ref_koids*/ zx_koid_t>> touch_targets_;

  // Tracks the View each mouse pointer is delivered to; a map from device ID to a ViewRef KOID.
  // This is used to ensure consistent delivery of mouse events for a given device. A focus change
  // triggered by other pointer events should *not* affect delivery of events to existing mice.
  std::unordered_map<uint32_t, std::vector</*view_ref_koids*/ zx_koid_t>> mouse_targets_;

  // Mapping of {device_id, pointer_id} to stream id for gfx legacy injection.
  std::unordered_map<uint64_t, StreamId> gfx_legacy_streams_;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_INPUT_SYSTEM_H_
