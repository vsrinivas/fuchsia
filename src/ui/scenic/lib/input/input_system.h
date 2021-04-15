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

#include "src/ui/scenic/lib/input/a11y_legacy_contender.h"
#include "src/ui/scenic/lib/input/gesture_arena.h"
#include "src/ui/scenic/lib/input/gfx_legacy_contender.h"
#include "src/ui/scenic/lib/input/injector.h"
#include "src/ui/scenic/lib/input/input_command_dispatcher.h"
#include "src/ui/scenic/lib/scenic/system.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

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

  explicit InputSystem(SystemContext context, fxl::WeakPtr<gfx::SceneGraph> scene_graph,
                       bool pointer_auto_focus);
  ~InputSystem() override = default;

  CommandDispatcherUniquePtr CreateCommandDispatcher(
      scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
      std::shared_ptr<ErrorReporter> error_reporter) override;

  // |fuchsia.ui.pointerinjector.Registry|
  void Register(fuchsia::ui::pointerinjector::Config config,
                fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> injector,
                RegisterCallback callback) override;

  fuchsia::ui::input::accessibility::PointerEventListenerPtr& accessibility_pointer_event_listener()
      const {
    return pointer_event_registry_->accessibility_pointer_event_listener();
  }

  void OnNewViewTreeSnapshot(std::shared_ptr<const view_tree::Snapshot> snapshot) {
    view_tree_snapshot_ = std::move(snapshot);
  }

  // |fuchsia.ui.pointercapture.ListenerRegistry|
  void RegisterListener(
      fidl::InterfaceHandle<fuchsia::ui::input::PointerCaptureListener> listener_handle,
      fuchsia::ui::views::ViewRef view_ref, RegisterListenerCallback success_callback) override;

  void DispatchPointerCommand(const fuchsia::ui::input::SendPointerInputCmd& command,
                              scheduling::SessionId session_id);

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
  // Perform a hit test with |event| in |view_tree| and returns the koids of all hit views, in order
  // from closest to furthest.
  std::vector<zx_koid_t> HitTest(const InternalPointerEvent& event, bool semantic_hit_test) const;

  // Injects a touch event directly to the View with koid |target|.
  void InjectTouchEventExclusive(const InternalPointerEvent& event);

  // Injects a touch event by hit testing for appropriate targets.
  void InjectTouchEventHitTested(const InternalPointerEvent& event, StreamId stream_id);
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
  // TODO(fxbug.dev/48150): Delete when we delete the PointerCapture functionality.
  void ReportPointerEventToPointerCaptureListener(const InternalPointerEvent& event) const;

  // Enqueue the pointer event into the EventReporter of a View.
  void ReportPointerEventToGfxLegacyView(const InternalPointerEvent& event, zx_koid_t view_ref_koid,
                                         fuchsia::ui::input::PointerEventType type) const;

  // Takes a ViewRef koid and creates a GfxLegacyContender that delivers events to the corresponding
  // SessionListener on contest victory.
  ContenderId AddGfxLegacyContender(StreamId stream_id, zx_koid_t view_ref_koid);

  fuchsia::ui::input::accessibility::PointerEvent CreateAccessibilityEvent(
      const InternalPointerEvent& event);

  // Collects all the GestureContenders for a new touch event stream.
  std::vector<ContenderId> CollectContenders(StreamId stream_id, const InternalPointerEvent& event);

  // Updates the gesture arena and all contenders for stream |stream_id| with a new event.
  void UpdateGestureContest(const InternalPointerEvent& event, StreamId stream_id);

  // Records a set of responses from a gesture disambiguation contender.
  void RecordGestureDisambiguationResponse(StreamId stream_id, ContenderId contender_id,
                                           const std::vector<GestureResponse>& responses);

  // Destroy the arena if the contest is complete (i.e. no contenders left or contest over and
  // stream ended).
  void DestroyArenaIfComplete(StreamId stream_id);

  // Helper methods for getting transforms out of |view_tree_snapshot_|. Return std::nullopt if the
  // passed in koids weren't represented in the |view_tree_snapshot_|.

  // Returns the transform from world space to view space.
  std::optional<glm::mat4> GetViewFromWorldTransform(zx_koid_t view_ref_koid) const;
  // Returns the transform from view space to world space.
  std::optional<glm::mat4> GetWorldFromViewTransform(zx_koid_t view_ref_koid) const;
  // Returns the transform from source view space to destination view space.
  std::optional<glm::mat4> GetDestinationViewFromSourceViewTransform(zx_koid_t source,
                                                                     zx_koid_t destination) const;

  // Determines whether focus should be automatically changed by pointer input.
  const bool pointer_auto_focus_;

  using InjectorId = uint64_t;
  InjectorId last_injector_id_ = 0;
  std::map<InjectorId, Injector> injectors_;

  fxl::WeakPtr<gfx::SceneGraph> scene_graph_;

  std::unique_ptr<A11yPointerEventRegistry> pointer_event_registry_;

  fidl::BindingSet<fuchsia::ui::pointerinjector::Registry> injector_registry_;

  fidl::BindingSet<fuchsia::ui::input::PointerCaptureListenerRegistry> pointer_capture_registry_;
  // A singleton listener who wants to be notified when pointer events happen.
  // We honor the first pointer capture listener to register. A call to RegisterListener()
  // above will fail if there is already a registered listener.
  std::optional<PointerCaptureListener> pointer_capture_listener_;

  // Tracks the View each mouse pointer is delivered to; a map from device ID to a ViewRef KOID.
  // This is used to ensure consistent delivery of mouse events for a given device. A focus change
  // triggered by other pointer events should *not* affect delivery of events to existing mice.
  std::unordered_map<uint32_t, std::vector</*view_ref_koids*/ zx_koid_t>> mouse_targets_;

  // GestureContender for the accessibility. Defined while a11y is active, null otherwise.
  std::unique_ptr<A11yLegacyContender> a11y_legacy_contender_;
  ContenderId a11y_contender_id_ = 1;

  // Mapping of {device_id, pointer_id} to stream id for gfx legacy injection.
  std::map<std::pair<uint32_t, uint32_t>, StreamId> gfx_legacy_streams_;
  std::unordered_map<ContenderId, GfxLegacyContender> gfx_legacy_contenders_;
  ContenderId next_contender_id_ = 2;

  // Map of all active contenders. If any contender is deleted, it must be removed from this map.
  std::unordered_map<ContenderId, GestureContender*> contenders_;

  std::unordered_map<StreamId, GestureArena> gesture_arenas_;

  // Snapshot of the ViewTree. Replaced with a new snapshot on call to OnNewViewTreeSnapshot(),
  // which happens once per rendered frame. This is the source of truth for the state of the
  // graphics system.
  std::shared_ptr<const view_tree::Snapshot> view_tree_snapshot_ =
      std::make_shared<const view_tree::Snapshot>();
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_INPUT_SYSTEM_H_
