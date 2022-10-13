// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_TOUCH_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_INPUT_TOUCH_SYSTEM_H_

#include <fuchsia/ui/pointer/augment/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <map>
#include <optional>

#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"
#include "src/ui/scenic/lib/input/a11y_legacy_contender.h"
#include "src/ui/scenic/lib/input/a11y_registry.h"
#include "src/ui/scenic/lib/input/constants.h"
#include "src/ui/scenic/lib/input/gesture_arena.h"
#include "src/ui/scenic/lib/input/gfx_legacy_contender.h"
#include "src/ui/scenic/lib/input/helper.h"
#include "src/ui/scenic/lib/input/hit_tester.h"
#include "src/ui/scenic/lib/input/touch_source_base.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace scenic_impl::input {

// Tracks input APIs.
class TouchSystem : public fuchsia::ui::pointer::augment::LocalHit {
 public:
  explicit TouchSystem(sys::ComponentContext* context,
                       std::shared_ptr<const view_tree::Snapshot>& view_tree_snapshot,
                       HitTester& hit_tester, inspect::Node& parent_node,
                       fxl::WeakPtr<gfx::SceneGraph> scene_graph);
  ~TouchSystem() = default;

  fuchsia::ui::input::accessibility::PointerEventListenerPtr&
  accessibility_pointer_event_listener() {
    return a11y_pointer_event_registry_->accessibility_pointer_event_listener();
  }

  void RegisterTouchSource(
      fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource> touch_source_request,
      zx_koid_t client_view_ref_koid);

  // |fuchsia::ui::pointer::augment::LocalHit|
  void Upgrade(fidl::InterfaceHandle<fuchsia::ui::pointer::TouchSource> original,
               fuchsia::ui::pointer::augment::LocalHit::UpgradeCallback callback) override;

  // For tests.
  // TODO(fxbug.dev/72919): Remove when integration tests are properly separated out.
  void RegisterA11yListener(
      fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener> listener,
      A11yPointerEventRegistry::RegisterCallback callback) {
    a11y_pointer_event_registry_->Register(std::move(listener), std::move(callback));
  }

  // Injects a touch event directly to the View with koid |event.target|.
  void InjectTouchEventExclusive(const InternalTouchEvent& event, StreamId stream_id);
  // Injects a touch event by hit testing for appropriate targets.
  void InjectTouchEventHitTested(const InternalTouchEvent& event, StreamId stream_id);

 private:
  // Finds the ViewRef koid registered with the other side of the |original| channel and returns it.
  // Returns ZX_KOID_INVALID if the related channel isn't found.
  zx_koid_t FindViewRefKoidOfRelatedChannel(
      const fidl::InterfaceHandle<fuchsia::ui::pointer::TouchSource>& original) const;

  // Enqueue the pointer event into the EventReporter of a View.
  void ReportPointerEventToGfxLegacyView(const InternalTouchEvent& event, zx_koid_t view_ref_koid,
                                         fuchsia::ui::input::PointerEventType type);

  // Takes a ViewRef koid and creates a GfxLegacyContender that delivers events to the corresponding
  // SessionListener on contest victory.
  ContenderId AddGfxLegacyContender(StreamId stream_id, zx_koid_t view_ref_koid);

  fuchsia::ui::input::accessibility::PointerEvent CreateAccessibilityEvent(
      const InternalTouchEvent& event);

  // Collects all the GestureContenders for a new touch event stream.
  std::vector<ContenderId> CollectContenders(StreamId stream_id, const InternalTouchEvent& event);

  // Updates the gesture arena and all contenders for stream |stream_id| with a new event.
  void UpdateGestureContest(const InternalTouchEvent& event, StreamId stream_id);

  // Records a set of responses from a gesture disambiguation contender.
  void RecordGestureDisambiguationResponse(StreamId stream_id, ContenderId contender_id,
                                           const std::vector<GestureResponse>& responses);

  // Destroy the arena if the contest is complete (i.e. no contenders left or contest over and
  // stream ended).
  void DestroyArenaIfComplete(StreamId stream_id);

  // Destroys contender specified by |contender_id| and removes it from all contests.
  void EraseContender(ContenderId contender_id, zx_koid_t view_ref_koid);

  // For a view hierarchy where |top| is an ancestor of |bottom|, returns |bottom|'s ancestor
  // hierarchy starting at |top| and ending at |bottom|.
  std::vector<zx_koid_t> GetAncestorChainTopToBottom(zx_koid_t bottom, zx_koid_t top) const;

  /// Construction-time state.
  // Reference to the ViewTreeSnapshot held by InputSystem.
  std::shared_ptr<const view_tree::Snapshot>& view_tree_snapshot_;
  HitTester& hit_tester_;
  // TODO(fxbug.dev/64206): Remove when we no longer have any legacy clients.
  fxl::WeakPtr<gfx::SceneGraph> scene_graph_;
  // An inspector that tracks all GestureContenders, so data can persist past contender lifetimes.
  // Must outlive all contenders.
  GestureContenderInspector contender_inspector_;

  /// FIDL server implementations.
  std::optional<A11yPointerEventRegistry> a11y_pointer_event_registry_;
  fidl::BindingSet<fuchsia::ui::pointer::augment::LocalHit> local_hit_upgrade_registry_;

  //// Gesture disambiguation state
  // Whenever a new touch event stream is started (by the injection of an ADD event) we create a
  // GestureArena to track that stream, and select a number of contenders to participate in the
  // contest. All contenders are tracked in the |contenders_| map for the duration of their
  // lifetime. The |contenders_| map is relied upon by the |gesture_arenas_| to deliver events.

  // Each gesture arena tracks one touch event stream and a set of contenders.
  std::unordered_map<StreamId, GestureArena> gesture_arenas_;

  // Map of all active contenders.
  std::unordered_map<ContenderId, std::unique_ptr<GestureContender>> contenders_;

  // Map of ViewRef koids to ContenderIds.
  // Does not include ContenderIds for A11yLegacyContender or GfxLegacyContender, since no View is
  // uniquely associated with either.
  std::unordered_map<zx_koid_t, ContenderId> viewrefs_to_contender_ids_;

  // Mapping of {device_id, pointer_id} to stream id for gfx legacy injection.
  std::map<std::pair<uint32_t, uint32_t>, StreamId> gfx_legacy_streams_;

  const ContenderId a11y_contender_id_ = 1;
  ContenderId next_contender_id_ = 2;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_TOUCH_SYSTEM_H_
