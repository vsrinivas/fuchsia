// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/touch_system.h"

#include <fidl/fuchsia.ui.pointer/cpp/fidl.h>
#include <fidl/fuchsia.ui.pointer/cpp/hlcpp_conversion.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/status.h>

#include <src/lib/fostr/fidl/fuchsia/ui/input/accessibility/formatting.h>
#include <src/lib/fostr/fidl/fuchsia/ui/input/formatting.h>

#include "src/ui/scenic/lib/input/constants.h"
#include "src/ui/scenic/lib/input/internal_pointer_event.h"
#include "src/ui/scenic/lib/input/touch_source.h"
#include "src/ui/scenic/lib/input/touch_source_with_local_hit.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/lib/utils/math.h"

#include <glm/glm.hpp>

namespace scenic_impl::input {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventType;

namespace {

// Helper function to build an AccessibilityPointerEvent when there is a
// registered accessibility listener.
AccessibilityPointerEvent BuildAccessibilityPointerEvent(const InternalTouchEvent& internal_event,
                                                         const glm::vec2& ndc_point,
                                                         const glm::vec2& local_point,
                                                         uint64_t viewref_koid) {
  AccessibilityPointerEvent event;
  event.set_event_time(internal_event.timestamp);
  event.set_device_id(internal_event.device_id);
  event.set_pointer_id(internal_event.pointer_id);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(InternalPhaseToGfxPhase(internal_event.phase));
  event.set_ndc_point({ndc_point.x, ndc_point.y});
  event.set_viewref_koid(viewref_koid);
  if (viewref_koid != ZX_KOID_INVALID) {
    event.set_local_point({local_point.x, local_point.y});
  }
  return event;
}

// Takes an InternalTouchEvent and returns a point in (Vulkan) Normalized Device Coordinates,
// in relation to the viewport. Intended for magnification
// TODO(fxbug.dev/50549): Only here to allow the legacy a11y flow. Remove along with the legacy a11y
// code.
glm::vec2 GetViewportNDCPoint(const InternalTouchEvent& internal_event) {
  const float width = internal_event.viewport.extents.max.x - internal_event.viewport.extents.min.x;
  const float height =
      internal_event.viewport.extents.max.y - internal_event.viewport.extents.min.y;
  return {
      width > 0 ? 2.f * internal_event.position_in_viewport.x / width - 1 : 0,
      height > 0 ? 2.f * internal_event.position_in_viewport.y / height - 1 : 0,
  };
}

void ChattyGfxLog(const fuchsia::ui::input::InputEvent& event) {
  static uint32_t chatty = 0;
  if (chatty++ < ChattyMax()) {
    FX_LOGS(INFO) << "Ptr-GFX[" << chatty << "/" << ChattyMax() << "]: " << event;
  }
}

void ChattyA11yLog(const fuchsia::ui::input::accessibility::PointerEvent& event) {
  static uint32_t chatty = 0;
  if (chatty++ < ChattyMax()) {
    FX_LOGS(INFO) << "Ptr-A11y[" << chatty << "/" << ChattyMax() << "]: " << event;
  }
}

}  // namespace

TouchSystem::TouchSystem(sys::ComponentContext* context,
                         std::shared_ptr<const view_tree::Snapshot>& view_tree_snapshot,
                         HitTester& hit_tester, inspect::Node& parent_node,
                         fxl::WeakPtr<gfx::SceneGraph> scene_graph)
    : view_tree_snapshot_(view_tree_snapshot),
      hit_tester_(hit_tester),
      scene_graph_(std::move(scene_graph)),
      contender_inspector_(parent_node.CreateChild("GestureContenders")) {
  a11y_pointer_event_registry_.emplace(
      context,
      /*on_register=*/
      [this] {
        FX_CHECK(!contenders_.count(a11y_contender_id_))
            << "on_disconnect must be called before registering a new listener";

        auto a11y_contender = std::make_unique<A11yLegacyContender>(
            /*respond*/
            [this](StreamId stream_id, GestureResponse response) {
              RecordGestureDisambiguationResponse(stream_id, a11y_contender_id_, {response});
            },
            /*deliver_to_client*/
            [this](const InternalTouchEvent& event) {
              std::vector<fuchsia::ui::input::accessibility::PointerEvent> a11y_events;
              a11y_events.push_back(CreateAccessibilityEvent(event));
              // Add in legacy UP and DOWN phases for ADD and REMOVE events respectively.
              const auto& original_event = a11y_events.front();
              if (original_event.phase() == fuchsia::ui::input::PointerEventPhase::ADD) {
                auto it = a11y_events.insert(a11y_events.end(), fidl::Clone(original_event));
                it->set_phase(fuchsia::ui::input::PointerEventPhase::DOWN);
              } else if (original_event.phase() == fuchsia::ui::input::PointerEventPhase::REMOVE) {
                auto it = a11y_events.insert(a11y_events.begin(), fidl::Clone(original_event));
                it->set_phase(fuchsia::ui::input::PointerEventPhase::UP);
              }

              for (auto& a11y_event : a11y_events) {
                ChattyA11yLog(a11y_event);
                accessibility_pointer_event_listener()->OnEvent(std::move(a11y_event));
              }
            },
            contender_inspector_);
        accessibility_pointer_event_listener().events().OnStreamHandled =
            [a11y_contender = a11y_contender.get()](
                uint32_t device_id, uint32_t pointer_id,
                fuchsia::ui::input::accessibility::EventHandling handled) {
              a11y_contender->OnStreamHandled(pointer_id, handled);
            };

        const auto [_, success] =
            contenders_.emplace(a11y_contender_id_, std::move(a11y_contender));
        FX_DCHECK(success) << "Duplicate A11yLegacyContender";
        FX_LOGS(INFO) << "A11yLegacyContender created.";
      },
      /*on_disconnect=*/
      [this] {
        FX_CHECK(contenders_.count(a11y_contender_id_)) << "can not disconnect before registering";
        // The listener disconnected. Release held events, delete the buffer.
        accessibility_pointer_event_listener().events().OnStreamHandled = nullptr;
        EraseContender(a11y_contender_id_, ZX_KOID_INVALID);
        FX_LOGS(INFO) << "A11yLegacyContender destroyed";
      });
  context->outgoing()->AddPublicService(local_hit_upgrade_registry_.GetHandler(this));
}

zx_koid_t TouchSystem::FindViewRefKoidOfRelatedChannel(
    const fidl::InterfaceHandle<fuchsia::ui::pointer::TouchSource>& original) const {
  const zx_koid_t related_koid = utils::ExtractRelatedKoid(original.channel());
  const auto it = std::find_if(
      contenders_.begin(), contenders_.end(),
      [related_koid](const auto& kv) { return kv.second->channel_koid() == related_koid; });
  return it == contenders_.end() ? ZX_KOID_INVALID : it->second->view_ref_koid_;
}

void TouchSystem::Upgrade(fidl::InterfaceHandle<fuchsia::ui::pointer::TouchSource> original,
                          fuchsia::ui::pointer::augment::LocalHit::UpgradeCallback callback) {
  // TODO(fxbug.dev/84270): This currently requires the client to wait until the TouchSource has
  // been hooked up before making the Upgrade() call. This is not a great user experience. Change
  // this so we cache the channel if it arrives too early.
  const zx_koid_t view_ref_koid = FindViewRefKoidOfRelatedChannel(original);
  if (view_ref_koid == ZX_KOID_INVALID) {
    auto error = fuchsia::ui::pointer::augment::ErrorForLocalHit::New();
    error->error_reason = fuchsia::ui::pointer::augment::ErrorReason::DENIED;
    error->original = std::move(original);
    callback({}, std::move(error));
    return;
  }

  // Delete the contender for the old channel.
  EraseContender(viewrefs_to_contender_ids_.at(view_ref_koid), view_ref_koid);

  // Create the new channel contender.
  const ContenderId contender_id = next_contender_id_++;
  fidl::InterfaceHandle<fuchsia::ui::pointer::augment::TouchSourceWithLocalHit> handle;
  {
    const auto [_, success] = contenders_.emplace(
        contender_id,
        std::make_unique<TouchSourceWithLocalHit>(
            view_ref_koid, handle.NewRequest(),
            /*respond*/
            [this, contender_id](StreamId stream_id,
                                 const std::vector<GestureResponse>& responses) {
              RecordGestureDisambiguationResponse(stream_id, contender_id, responses);
            },
            /*error_handler*/
            [this, contender_id, view_ref_koid] { EraseContender(contender_id, view_ref_koid); },
            /*get_local_hit*/
            [this](const InternalTouchEvent& event) {
              // Perform a semantic hit test to find the top view a11y cares about.
              // TODO(fxbug.dev/106611): If we have more than one TouchSourceWithLocalHit client,
              // this hit test will be done multiple times per injectiom redundantly. We might need
              // to improve this in the future, but as long as we're only expecting the one client
              // this is fine.
              const zx_koid_t top_koid = hit_tester_.TopHitTest(event, /*semantic_hit_test*/ true);
              glm::vec2 local_point = glm::vec2(0.f, 0.f);
              if (top_koid != ZX_KOID_INVALID) {
                const std::array<float, 9> top_view_from_viewport_transform =
                    GetDestinationFromViewportTransform(event, top_koid, *view_tree_snapshot_);
                local_point = utils::TransformPointerCoords(
                    event.position_in_viewport,
                    utils::ColumnMajorMat3ArrayToMat4(top_view_from_viewport_transform));
              }
              return std::pair<zx_koid_t, std::array<float, 2>>{top_koid,
                                                                {local_point.x, local_point.y}};
            },
            contender_inspector_));
    FX_CHECK(success);
  }
  {
    const auto [_, success] = viewrefs_to_contender_ids_.emplace(view_ref_koid, contender_id);
    FX_CHECK(success);
  }

  // Return the new channel.
  callback(std::move(handle), nullptr);
}

fuchsia::ui::input::accessibility::PointerEvent TouchSystem::CreateAccessibilityEvent(
    const InternalTouchEvent& event) {
  // Find top-hit target and send it to accessibility.
  const zx_koid_t view_ref_koid = hit_tester_.TopHitTest(event, /*semantic_hit_test*/ true);

  glm::vec2 top_hit_view_local;
  if (view_ref_koid != ZX_KOID_INVALID) {
    std::optional<glm::mat4> view_from_context =
        view_tree_snapshot_->GetDestinationViewFromSourceViewTransform(
            /*source*/ event.context, /*destination*/ view_ref_koid);
    FX_DCHECK(view_from_context)
        << "could only happen if the view_tree_view_tree_snapshot_ was updated "
           "between the event arriving and now";

    const glm::mat4 view_from_viewport =
        view_from_context.value() * event.viewport.context_from_viewport_transform;
    top_hit_view_local =
        utils::TransformPointerCoords(event.position_in_viewport, view_from_viewport);
  }
  const glm::vec2 ndc = GetViewportNDCPoint(event);

  return BuildAccessibilityPointerEvent(event, ndc, top_hit_view_local, view_ref_koid);
}

ContenderId TouchSystem::AddGfxLegacyContender(StreamId stream_id, zx_koid_t view_ref_koid) {
  FX_DCHECK(view_ref_koid != ZX_KOID_INVALID);

  const ContenderId contender_id = next_contender_id_++;
  auto [_, success] = contenders_.emplace(
      contender_id, std::make_unique<GfxLegacyContender>(
                        view_ref_koid,
                        /*respond*/
                        [this, stream_id, contender_id](GestureResponse response) {
                          RecordGestureDisambiguationResponse(stream_id, contender_id, {response});
                        },
                        /*deliver_events_to_client*/
                        [this, view_ref_koid](const std::vector<InternalTouchEvent>& events) {
                          for (const auto& event : events) {
                            ReportPointerEventToGfxLegacyView(
                                event, view_ref_koid, fuchsia::ui::input::PointerEventType::TOUCH);
                          }
                        },
                        /*self_destruct*/
                        [this, contender_id] { EraseContender(contender_id, ZX_KOID_INVALID); },
                        contender_inspector_));
  FX_DCHECK(success);
  return contender_id;
}

void TouchSystem::RegisterTouchSource(
    fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource> touch_source_request,
    zx_koid_t client_view_ref_koid) {
  RegisterTouchSource(fidl::HLCPPToNatural(std::move(touch_source_request)), client_view_ref_koid);
}

void TouchSystem::RegisterTouchSource(
    fidl::ServerEnd<fuchsia_ui_pointer::TouchSource> touch_source_server_end,
    zx_koid_t client_view_ref_koid) {
  FX_DCHECK(client_view_ref_koid != ZX_KOID_INVALID);
  const ContenderId contender_id = next_contender_id_++;

  // Note: These closure must'nt be called in the constructor, since they depend on the
  // |contenders_| map, which isn't filled until after construction completes.
  {
    const auto [_, success] = contenders_.emplace(
        contender_id, std::make_unique<TouchSource>(
                          client_view_ref_koid, std::move(touch_source_server_end),
                          /*respond*/
                          [this, contender_id](StreamId stream_id,
                                               const std::vector<GestureResponse>& responses) {
                            RecordGestureDisambiguationResponse(stream_id, contender_id, responses);
                          },
                          /*error_handler*/
                          [this, contender_id, client_view_ref_koid] {
                            EraseContender(contender_id, client_view_ref_koid);
                          },
                          contender_inspector_));
    FX_DCHECK(success);
  }
  {
    const auto [_, success] =
        viewrefs_to_contender_ids_.emplace(client_view_ref_koid, contender_id);
    FX_DCHECK(success);
  }
}

void TouchSystem::InjectTouchEventExclusive(const InternalTouchEvent& event, StreamId stream_id) {
  if (view_tree_snapshot_->view_tree.count(event.target) == 0 &&
      view_tree_snapshot_->unconnected_views.count(event.target) == 0) {
    FX_DCHECK(contenders_.count(event.target) == 0);
    return;
  }
  FX_DCHECK(event.phase == Phase::kCancel ||
            view_tree_snapshot_->IsDescendant(event.target, event.context))
      << "Should never allow injection of non-cancel events into broken scene graph";

  auto it = viewrefs_to_contender_ids_.find(event.target);
  if (it != viewrefs_to_contender_ids_.end()) {
    auto& contender = *contenders_.at(it->second);
    // Calling EndContest() before the first event causes them to be combined in the first message
    // to the client.
    if (event.phase == Phase::kAdd) {
      contender.EndContest(stream_id, /*awarded_win=*/true);
    }

    // If the target is not in the view tree then this must be a cancel event and we don't need to
    // (and can't) supply correct transforms and bounding boxes.
    if (view_tree_snapshot_->view_tree.count(event.target) == 0) {
      FX_DCHECK(event.phase == Phase::kCancel);
      contender.UpdateStream(stream_id, event, /*is_end_of_stream=*/true, /*bounding_box=*/{});
    } else {
      contender.UpdateStream(
          stream_id,
          EventWithReceiverFromViewportTransform(event, event.target, *view_tree_snapshot_),
          /*is_end_of_stream=*/event.phase == Phase::kRemove || event.phase == Phase::kCancel,
          view_tree_snapshot_->view_tree.at(event.target).bounding_box);
    }
  } else {
    // If there is no TouchContender for the target, then we assume it to be a GfxLegacyContender.
    ReportPointerEventToGfxLegacyView(event, event.target,
                                      fuchsia::ui::input::PointerEventType::TOUCH);
  }
}

// The touch state machine comprises ADD/DOWN/MOVE*/UP/REMOVE. Some notes:
//  - We assume one touchscreen device, and use the device-assigned finger ID.
//  - Touch ADD associates the following ADD/DOWN/MOVE*/UP/REMOVE event sequence
//    with the set of clients available at that time. To enable gesture
//    disambiguation, we perform parallel dispatch to all clients.
//  - Touch DOWN triggers a focus change, honoring the "may receive focus" property.
//  - Touch REMOVE drops the association between event stream and client.
void TouchSystem::InjectTouchEventHitTested(const InternalTouchEvent& event, StreamId stream_id) {
  // New stream. Collect contenders and set up a new arena.
  if (event.phase == Phase::kAdd) {
    std::vector<ContenderId> contenders = CollectContenders(stream_id, event);
    if (!contenders.empty()) {
      const bool is_single_contender = contenders.size() == 1;
      const ContenderId front_contender = contenders.front();
      const auto [it, success] =
          gesture_arenas_.emplace(stream_id, GestureArena{std::move(contenders)});
      FX_DCHECK(success);
      // If there's only a single contender then the contest is already decided
      FX_DCHECK(it->second.contest_has_ended() == is_single_contender);
      if (it->second.contest_has_ended()) {
        contenders_.at(front_contender)->EndContest(stream_id, /*awarded_win*/ true);
      }
    }
  }

  // No arena means the contest is over and no one won.
  if (!gesture_arenas_.count(stream_id)) {
    return;
  }

  UpdateGestureContest(event, stream_id);
}

static bool IsRootOrDirectChildOfRoot(zx_koid_t koid, const view_tree::Snapshot& snapshot) {
  if (snapshot.root == koid) {
    return true;
  }
  if (snapshot.view_tree.count(koid) == 0) {
    return false;
  }

  return snapshot.view_tree.at(koid).parent == snapshot.root;
}

std::vector<zx_koid_t> TouchSystem::GetAncestorChainTopToBottom(zx_koid_t bottom,
                                                                zx_koid_t top) const {
  if (bottom == top) {
    return {bottom};
  }

  // Get ancestors bottom closest to furthest.
  std::vector<zx_koid_t> ancestors = view_tree_snapshot_->GetAncestorsOf(bottom);
  FX_DCHECK(ancestors.empty() || std::any_of(ancestors.begin(), ancestors.end(),
                                             [top](const zx_koid_t koid) { return koid == top; }))
      << "|top| must be an ancestor of |bottom|";

  // Remove all ancestors after |top|.
  for (auto it = ancestors.begin(); it != ancestors.end(); ++it) {
    if (*it == top) {
      ancestors.erase(++it, ancestors.end());
      break;
    }
  }

  // Reverse the list and add |bottom| to the end.
  std::reverse(ancestors.begin(), ancestors.end());
  ancestors.emplace_back(bottom);
  FX_DCHECK(ancestors.front() == top);

  return ancestors;
}

std::vector<ContenderId> TouchSystem::CollectContenders(StreamId stream_id,
                                                        const InternalTouchEvent& event) {
  std::vector<ContenderId> contenders;

  // Add an A11yLegacyContender if the injection context is the root of the ViewTree.
  // TODO(fxbug.dev/50549): Remove when a11y is a native GD client.
  if (contenders_.count(a11y_contender_id_) &&
      IsRootOrDirectChildOfRoot(event.context, *view_tree_snapshot_)) {
    contenders.push_back(a11y_contender_id_);
  }

  const zx_koid_t top_koid = hit_tester_.TopHitTest(event, /*semantic_hit_test*/ false);
  if (top_koid != ZX_KOID_INVALID) {
    // Find TouchSource contenders in priority order from furthest (valid) ancestor to top hit view.
    const std::vector<zx_koid_t> ancestors = GetAncestorChainTopToBottom(top_koid, event.target);
    for (const auto koid : ancestors) {
      // If a touch contender doesn't exist it means the client didn't provide a TouchSource
      // endpoint.
      const auto it = viewrefs_to_contender_ids_.find(koid);
      if (it != viewrefs_to_contender_ids_.end()) {
        const ContenderId contender_id = it->second;
        FX_DCHECK(contenders_.count(contender_id));
        contenders.push_back(contender_id);
      }
    }

    // Add a GfxLegacyContender if we didn't find a corresponding TouchSource contender for the top
    // hit view.
    // TODO(fxbug.dev/64206): Remove when we no longer have any legacy clients.
    if (!viewrefs_to_contender_ids_.count(top_koid)) {
      FX_VLOGS(1) << "View hit: [ViewRefKoid=" << top_koid << "]";
      const ContenderId contender_id = AddGfxLegacyContender(stream_id, top_koid);
      contenders.push_back(contender_id);
    }
  }

  return contenders;
}

void TouchSystem::UpdateGestureContest(const InternalTouchEvent& event, StreamId stream_id) {
  const auto arena_it = gesture_arenas_.find(stream_id);
  if (arena_it == gesture_arenas_.end()) {
    // Contest already ended, with no winner.
    return;
  }
  auto& arena = arena_it->second;

  const bool is_end_of_stream = event.phase == Phase::kRemove || event.phase == Phase::kCancel;
  arena.UpdateStream(/*length*/ 1, is_end_of_stream);

  // Update remaining contenders.
  // Copy the vector to avoid problems if the arena is destroyed inside of UpdateStream().
  const std::vector<ContenderId> contenders = arena.contenders();
  const glm::mat4 world_from_viewport_transform =
      view_tree_snapshot_->GetWorldFromViewTransform(event.context).value() *
      event.viewport.context_from_viewport_transform;
  for (const auto contender_id : contenders) {
    // Don't use the arena obtained above the loop, because it may have been removed from
    // gesture_arenas_ in a previous loop iteration.
    // TODO(fxbug.dev/90004): it would be nice to restructure the code so that the arena can be
    // obtained once at the top of this method, and guaranteed to be safe to reuse thereafter.
    const auto arena_it = gesture_arenas_.find(stream_id);
    if (arena_it == gesture_arenas_.end()) {
      // Break out of the loop: if we didn't find the arena in this iteration, we won't find it in
      // subsequent iterations either.
      break;
    }
    if (arena_it->second.contest_has_ended() && !arena_it->second.contains(contender_id)) {
      // Contest ended with this contender not being the winner; no need to consider it further.
      continue;
    }
    const auto it = contenders_.find(contender_id);
    if (it == contenders_.end()) {
      // This contender is no longer present, probably because the client has disconnected.
      // TODO(fxbug.dev/90004): the contender is still in the arena, though.  Can this cause
      // problems (such as the arena contest never completing), or will the arena soon finish and be
      // deleted anyway?
      continue;
    }

    GestureContender& contender = *it->second;
    const zx_koid_t view_ref_koid = contender.view_ref_koid_;
    if (view_tree_snapshot_->view_tree.count(view_ref_koid) != 0) {
      // Everything is fine. Send as normal.
      contender.UpdateStream(stream_id,
                             EventWithReceiverFromViewportTransform(
                                 event, /*destination=*/view_ref_koid, *view_tree_snapshot_),
                             is_end_of_stream,
                             view_tree_snapshot_->view_tree.at(view_ref_koid).bounding_box);
    } else if (contender_id == a11y_contender_id_) {
      // TODO(fxbug.dev/50549): A11yLegacyContender doesn't need correct transforms or view bounds.
      // Remove this branch when legacy a11y api goes away.
      contender.UpdateStream(stream_id, event, is_end_of_stream, /*bounding_box=*/{});
    } else {
      // Contender not in the view tree -> cancel the rest of the stream for that contender.
      auto& arena = arena_it->second;
      if (!arena.contest_has_ended()) {
        // Contest ongoing -> just send a no response on behalf of |contender_id|.
        RecordGestureDisambiguationResponse(stream_id, contender_id, {GestureResponse::kNo});
        FX_DCHECK(gesture_arenas_.count(stream_id) == 0 || !arena.contains(contender_id));
      } else {
        // Contest ended -> Need to send an explicit "cancel" event to the contender.
        FX_DCHECK(arena.contenders().size() == 1 && arena.contains(contender_id));
        FX_DCHECK(event.phase != Phase::kAdd);
        InternalTouchEvent event_copy = event;
        event_copy.phase = Phase::kCancel;
        contender.UpdateStream(stream_id, event_copy, /*is_end_of_stream=*/true,
                               /*bounding_box=*/{});
        // The contest is definitely over, so we can manually destroy the arena here.
        gesture_arenas_.erase(stream_id);
        break;
      }
    }
  }

  DestroyArenaIfComplete(stream_id);
}

void TouchSystem::RecordGestureDisambiguationResponse(
    StreamId stream_id, ContenderId contender_id, const std::vector<GestureResponse>& responses) {
  auto arena_it = gesture_arenas_.find(stream_id);
  if (arena_it == gesture_arenas_.end() || !arena_it->second.contains(contender_id)) {
    return;
  }
  auto& arena = arena_it->second;

  // No need to record after the contest has ended.
  if (!arena.contest_has_ended()) {
    // Update the arena.
    const ContestResults result = arena.RecordResponses(contender_id, responses);
    for (auto loser_id : result.losers) {
      // Need to check for existence, since a loser could be the result of a NO response upon
      // destruction.
      auto contender = contenders_.find(loser_id);
      if (contender != contenders_.end()) {
        contenders_.at(loser_id)->EndContest(stream_id, /*awarded_win*/ false);
      }
    }
    if (result.winner) {
      FX_DCHECK(arena.contenders().size() == 1u);
      contenders_.at(result.winner.value())->EndContest(stream_id, /*awarded_win*/ true);
    }
  }

  DestroyArenaIfComplete(stream_id);
}

void TouchSystem::DestroyArenaIfComplete(StreamId stream_id) {
  const auto arena_it = gesture_arenas_.find(stream_id);
  if (arena_it == gesture_arenas_.end()) {
    return;
  }

  const auto& arena = arena_it->second;

  // This branch will eventually be taken for every arena.
  // TODO(fxbug.dev/90004): can we elaborate on why this is true?
  if (arena.contenders().empty() || (arena.contest_has_ended() && arena.stream_has_ended())) {
    gesture_arenas_.erase(stream_id);
  }
}

void TouchSystem::EraseContender(ContenderId contender_id, zx_koid_t view_ref_koid) {
  {
    const size_t success = contenders_.erase(contender_id);
    FX_DCHECK(success) << "Contender " << contender_id << " did not exist";
  }
  // TODO(fxbug.dev/64376): ZX_KOID_INVALID is only passed in by legacy contenders. Remove this
  // check when they go away.
  if (view_ref_koid != ZX_KOID_INVALID) {
    const size_t success = viewrefs_to_contender_ids_.erase(view_ref_koid);
    FX_DCHECK(success) << "ViewRef " << view_ref_koid << " was not mapped to a ContenderId";
  }

  // Remove from any contests it may still be a part of.
  // Note: Need to finish walking the arena map before we start calling RecordGDResponse() since
  // it may invalidate the iterator.
  std::vector<StreamId> ongoing_streams;
  for (const auto& [stream_id, arena] : gesture_arenas_) {
    const auto contenders = arena.contenders();
    if (std::count(contenders.begin(), contenders.end(), contender_id)) {
      ongoing_streams.push_back(stream_id);
    }
  }
  for (const auto stream_id : ongoing_streams) {
    RecordGestureDisambiguationResponse(stream_id, contender_id, {GestureResponse::kNo});
  }
}

void TouchSystem::ReportPointerEventToGfxLegacyView(const InternalTouchEvent& event,
                                                    zx_koid_t view_ref_koid,
                                                    fuchsia::ui::input::PointerEventType type) {
  TRACE_DURATION("input", "dispatch_event_to_client", "event_type", "pointer");
  if (!scene_graph_)
    return;

  auto event_reporter = scene_graph_->view_tree().EventReporterOf(view_ref_koid);
  if (!event_reporter)
    return;

  if (view_tree_snapshot_->view_tree.count(view_ref_koid) == 0)
    return;

  const uint64_t trace_id = TRACE_NONCE();
  TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", trace_id);

  std::vector<fuchsia::ui::input::PointerEvent> gfx_pointer_events;
  gfx_pointer_events.push_back(InternalTouchEventToGfxPointerEvent(
      EventWithReceiverFromViewportTransform(event, /*destination=*/view_ref_koid,
                                             *view_tree_snapshot_),
      type, trace_id));

  // Add in legacy UP and DOWN phases for ADD and REMOVE events respectively.
  const auto& original_event = gfx_pointer_events.front();
  if (original_event.phase == fuchsia::ui::input::PointerEventPhase::ADD) {
    auto it = gfx_pointer_events.insert(gfx_pointer_events.end(), fidl::Clone(original_event));
    it->phase = fuchsia::ui::input::PointerEventPhase::DOWN;
  } else if (original_event.phase == fuchsia::ui::input::PointerEventPhase::REMOVE) {
    auto it = gfx_pointer_events.insert(gfx_pointer_events.begin(), fidl::Clone(original_event));
    it->phase = fuchsia::ui::input::PointerEventPhase::UP;
  }

  for (auto& event : gfx_pointer_events) {
    InputEvent input_event;
    input_event.set_pointer(std::move(event));
    FX_VLOGS(1) << "Event dispatch to view=" << view_ref_koid << ": " << input_event;
    ChattyGfxLog(input_event);
    contender_inspector_.OnInjectedEvents(view_ref_koid, 1);
    event_reporter->EnqueueEvent(std::move(input_event));
  }
}

}  // namespace scenic_impl::input
