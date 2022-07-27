// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/touch_system.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <src/lib/fostr/fidl/fuchsia/ui/input/accessibility/formatting.h>
#include <src/lib/fostr/fidl/fuchsia/ui/input/formatting.h>

#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/input/constants.h"
#include "src/ui/scenic/lib/input/internal_pointer_event.h"
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
                         fit::function<void(zx_koid_t)> request_focus,
                         fxl::WeakPtr<gfx::SceneGraph> scene_graph)
    : view_tree_snapshot_(view_tree_snapshot),
      hit_tester_(hit_tester),
      request_focus_(std::move(request_focus)),
      scene_graph_(std::move(scene_graph)),
      contender_inspector_(parent_node.CreateChild("GestureContenders")) {
  a11y_pointer_event_registry_.emplace(
      context,
      /*on_register=*/
      [this] {
        FX_CHECK(!a11y_legacy_contender_)
            << "on_disconnect must be called before registering a new listener";

        a11y_legacy_contender_ = std::make_unique<A11yLegacyContender>(
            /*respond*/
            [this](StreamId stream_id, GestureResponse response) {
              RecordGestureDisambiguationResponse(stream_id, a11y_contender_id_, {response});
            },
            /*deliver_to_client*/
            [this](const InternalTouchEvent& event) {
              auto a11y_event = CreateAccessibilityEvent(event);
              ChattyA11yLog(a11y_event);
              accessibility_pointer_event_listener()->OnEvent(std::move(a11y_event));
            },
            contender_inspector_);
        FX_LOGS(INFO) << "A11yLegacyContender created.";
        contenders_.emplace(a11y_contender_id_, a11y_legacy_contender_.get());

        accessibility_pointer_event_listener().events().OnStreamHandled =
            [this](uint32_t device_id, uint32_t pointer_id,
                   fuchsia::ui::input::accessibility::EventHandling handled) {
              FX_DCHECK(a11y_legacy_contender_);
              a11y_legacy_contender_->OnStreamHandled(pointer_id, handled);
            };
      },
      /*on_disconnect=*/
      [this] {
        FX_CHECK(a11y_legacy_contender_) << "can not disconnect before registering";
        // The listener disconnected. Release held events, delete the buffer.
        accessibility_pointer_event_listener().events().OnStreamHandled = nullptr;
        contenders_.erase(a11y_contender_id_);
        a11y_legacy_contender_.reset();
        FX_LOGS(INFO) << "A11yLegacyContender destroyed";
      });
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
  auto [contender_it, success] = gfx_legacy_contenders_.try_emplace(
      contender_id, view_ref_koid,
      /*respond*/
      [this, stream_id, contender_id](GestureResponse response) {
        RecordGestureDisambiguationResponse(stream_id, contender_id, {response});
      },
      /*deliver_events_to_client*/
      [this, view_ref_koid](const std::vector<InternalTouchEvent>& events) {
        for (const auto& event : events) {
          ReportPointerEventToGfxLegacyView(event, view_ref_koid,
                                            fuchsia::ui::input::PointerEventType::TOUCH);

          // Update focus if necessary.
          // TODO(fxbug.dev/59858): Figure out how to handle focus with real GD clients.
          if (event.phase == Phase::kAdd) {
            if (view_tree_snapshot_->view_tree.count(view_ref_koid) != 0) {
              if (view_tree_snapshot_->view_tree.at(view_ref_koid).is_focusable) {
                request_focus_(view_ref_koid);
              }
            } else {
              // Focus root.
              request_focus_(ZX_KOID_INVALID);
            }
          }
        }
      },
      /*self_destruct*/
      [this, contender_id] {
        contenders_.erase(contender_id);
        gfx_legacy_contenders_.erase(contender_id);
      },
      contender_inspector_);
  FX_DCHECK(success);
  contenders_.emplace(contender_id, &contender_it->second);
  return contender_id;
}

void TouchSystem::RegisterTouchSource(
    fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource> touch_source_request,
    zx_koid_t client_view_ref_koid) {
  FX_DCHECK(client_view_ref_koid != ZX_KOID_INVALID);
  const ContenderId contender_id = next_contender_id_++;

  // Note: These closure must'nt be called in the constructor, since they depend on the
  // |contenders_| map, which isn't filled until after construction completes.
  const auto [it, success1] = touch_contenders_.try_emplace(
      client_view_ref_koid, client_view_ref_koid, contender_id, std::move(touch_source_request),
      /*respond*/
      [this, contender_id](StreamId stream_id, const std::vector<GestureResponse>& responses) {
        RecordGestureDisambiguationResponse(stream_id, contender_id, responses);
      },
      /*error_handler*/
      [this, contender_id, client_view_ref_koid] {
        // Erase from |contenders_| first to avoid re-entry.
        contenders_.erase(contender_id);
        touch_contenders_.erase(client_view_ref_koid);
      },
      contender_inspector_);
  FX_DCHECK(success1);
  const auto [_, success2] = contenders_.emplace(contender_id, &it->second.touch_source);
  FX_DCHECK(success2);
}

void TouchSystem::DispatchPointerCommand(const fuchsia::ui::input::SendPointerInputCmd& command,
                                         scheduling::SessionId session_id) {
  TRACE_DURATION("input", "dispatch_command", "command", "PointerCmd");
  if (command.pointer_event.phase == fuchsia::ui::input::PointerEventPhase::HOVER) {
    FX_LOGS(WARNING) << "Injected pointer event had unexpected HOVER event.";
    return;
  }

  if (!scene_graph_) {
    FX_LOGS(INFO) << "SceneGraph wasn't set up before injecting legacy input. Dropping event.";
    return;
  }

  // Compositor and layer stack required for dispatch.
  const GlobalId compositor_id(session_id, command.compositor_id);
  gfx::CompositorWeakPtr compositor = scene_graph_->GetCompositor(compositor_id);
  if (!compositor) {
    FX_LOGS(INFO) << "Compositor wasn't set up before injecting legacy input. Dropping event.";
    return;  // It's legal to race against GFX's compositor setup.
  }

  gfx::LayerStackPtr layer_stack = compositor->layer_stack();
  if (!layer_stack) {
    FX_LOGS(INFO) << "Layer stack wasn't set up before injecting legacy input. Dropping event.";
    return;  // It's legal to race against GFX's layer stack setup.
  }

  const auto layers = layer_stack->layers();
  if (layers.empty()) {
    FX_LOGS(INFO) << "Layer wasn't set up before injecting legacy input. Dropping event.";
    return;
  }

  // Assume we only have one layer.
  const gfx::LayerPtr first_layer = *layers.begin();
  const std::optional<glm::mat4> world_from_screen_transform =
      first_layer->GetWorldFromScreenTransform();
  if (!world_from_screen_transform) {
    FX_LOGS(INFO) << "Wasn't able to get a WorldFromScreenTransform when injecting legacy input. "
                     "Dropping event. Is the camera or renderer uninitialized?";
    return;
  }

  const zx_koid_t root_koid = view_tree_snapshot_->root;
  if (root_koid == ZX_KOID_INVALID) {
    FX_LOGS(WARNING) << "Attempted to inject legacy input before scene setup";
    return;
  }

  const std::optional<glm::mat4> context_from_world_transform =
      view_tree_snapshot_->GetViewFromWorldTransform(root_koid);
  FX_DCHECK(context_from_world_transform);

  const uint32_t screen_width = first_layer->width();
  const uint32_t screen_height = first_layer->height();
  if (screen_width == 0 || screen_height == 0) {
    FX_LOGS(WARNING) << "Attempted to inject legacy input while Layer had 0 area";
    return;
  }
  const glm::mat4 context_from_screen_transform =
      context_from_world_transform.value() * world_from_screen_transform.value();

  InternalTouchEvent internal_event = GfxPointerEventToInternalEvent(
      command.pointer_event, root_koid, static_cast<float>(screen_width),
      static_cast<float>(screen_height), context_from_screen_transform);

  switch (command.pointer_event.type) {
    case PointerEventType::TOUCH: {
      // Get stream id. Create one if this is a new stream.
      const std::pair<uint32_t, uint32_t> stream_key{internal_event.device_id,
                                                     internal_event.pointer_id};
      if (!gfx_legacy_streams_.count(stream_key)) {
        if (internal_event.phase != Phase::kAdd) {
          FX_LOGS(WARNING) << "Attempted to start a stream without an initial ADD.";
          return;
        }

        gfx_legacy_streams_.emplace(stream_key, NewStreamId());
      } else if (internal_event.phase == Phase::kAdd) {
        FX_LOGS(WARNING) << "Attempted to ADD twice for the same stream.";
        return;
      }
      const auto stream_id = gfx_legacy_streams_[stream_key];

      // Remove from ongoing streams on stream end.
      if (internal_event.phase == Phase::kRemove || internal_event.phase == Phase::kCancel) {
        gfx_legacy_streams_.erase(stream_key);
      }

      TRACE_DURATION("input", "dispatch_command", "command", "TouchCmd");
      TRACE_FLOW_END(
          "input", "dispatch_event_to_scenic",
          PointerTraceHACK(command.pointer_event.radius_major, command.pointer_event.radius_minor));
      InjectTouchEventHitTested(internal_event, stream_id);
      break;
    }
    case PointerEventType::MOUSE: {
      TRACE_DURATION("input", "dispatch_command", "command", "MouseCmd");
      if (internal_event.phase == Phase::kAdd || internal_event.phase == Phase::kRemove) {
        FX_LOGS(WARNING) << "Oops, mouse device (id=" << internal_event.device_id
                         << ") had an unexpected event: " << internal_event.phase;
        return;
      }
      LegacyInjectMouseEventHitTested(internal_event);
      break;
    }
    default:
      FX_LOGS(INFO) << "Stylus not supported by legacy input injection API.";
      break;
  }
}

void TouchSystem::InjectTouchEventExclusive(const InternalTouchEvent& event, StreamId stream_id) {
  if (view_tree_snapshot_->view_tree.count(event.target) == 0 &&
      view_tree_snapshot_->unconnected_views.count(event.target) == 0) {
    FX_DCHECK(touch_contenders_.count(event.target) == 0);
    return;
  }
  FX_DCHECK(event.phase == Phase::kCancel ||
            view_tree_snapshot_->IsDescendant(event.target, event.context))
      << "Should never allow injection of non-cancel events into broken scene graph";

  auto it = touch_contenders_.find(event.target);
  if (it != touch_contenders_.end()) {
    auto& touch_source = it->second.touch_source;
    // Calling EndContest() before the first event causes them to be combined in the first message
    // to the client.
    if (event.phase == Phase::kAdd) {
      touch_source.EndContest(stream_id, /*awarded_win=*/true);
    }

    // If the target is not in the view tree then this must be a cancel event and we don't need to
    // (and can't) supply correct transforms and bounding boxes.
    if (view_tree_snapshot_->view_tree.count(event.target) == 0) {
      FX_DCHECK(event.phase == Phase::kCancel);
      touch_source.UpdateStream(stream_id, event, /*is_end_of_stream=*/true, /*bounding_box=*/{});
    } else {
      touch_source.UpdateStream(
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
    } else {
      // No node was hit. Transfer focus to root.
      request_focus_(ZX_KOID_INVALID);
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
  FX_DCHECK(event.phase == Phase::kAdd);
  std::vector<ContenderId> contenders;

  // Add an A11yLegacyContender if the injection context is the root of the ViewTree.
  // TODO(fxbug.dev/50549): Remove when a11y is a native GD client.
  if (a11y_legacy_contender_ && IsRootOrDirectChildOfRoot(event.context, *view_tree_snapshot_)) {
    contenders.push_back(a11y_contender_id_);
  }

  const zx_koid_t top_koid = hit_tester_.TopHitTest(event, /*semantic_hit_test*/ false);
  if (top_koid != ZX_KOID_INVALID) {
    // Find TouchSource contenders in priority order from furthest (valid) ancestor to top hit view.
    std::vector<zx_koid_t> ancestors = GetAncestorChainTopToBottom(top_koid, event.target);
    for (auto koid : ancestors) {
      const auto it = touch_contenders_.find(koid);
      // If a touch contender doesn't exist it means the client didn't provide a TouchSource
      // endpoint.
      if (it != touch_contenders_.end()) {
        contenders.push_back(it->second.contender_id);
      }
    }

    // Add a GfxLegacyContender if we didn't find a corresponding TouchSource contender for the top
    // hit view.
    // TODO(fxbug.dev/64206): Remove when we no longer have any legacy clients.
    if (top_koid != ZX_KOID_INVALID && touch_contenders_.count(top_koid) == 0) {
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
    GestureContender* const contender_ptr = it->second;

    const zx_koid_t view_ref_koid = contender_ptr->view_ref_koid_;
    if (view_tree_snapshot_->view_tree.count(view_ref_koid) != 0) {
      // Everything is fine. Send as normal.
      contender_ptr->UpdateStream(stream_id,
                                  EventWithReceiverFromViewportTransform(
                                      event, /*destination=*/view_ref_koid, *view_tree_snapshot_),
                                  is_end_of_stream,
                                  view_tree_snapshot_->view_tree.at(view_ref_koid).bounding_box);
    } else if (contender_id == a11y_contender_id_) {
      // TODO(fxbug.dev/50549): A11yLegacyContender doesn't need correct transforms or view bounds.
      // Remove this branch when legacy a11y api goes away.
      contender_ptr->UpdateStream(stream_id, event, is_end_of_stream, /*bounding_box=*/{});
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
        contender_ptr->UpdateStream(stream_id, event_copy, /*is_end_of_stream=*/true,
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

  // One of these two branches will always be reached eventually.
  // TODO(fxbug.dev/90004): can we elaborate on why this is true?
  if (arena.contenders().empty()) {
    // If no one won the contest then it will appear as if nothing was hit. Transfer focus to root.
    // TODO(fxbug.dev/59858): This probably needs to change when we figure out the exact semantics
    // we want.
    request_focus_(ZX_KOID_INVALID);
    gesture_arenas_.erase(stream_id);
  } else if (arena.contest_has_ended() && arena.stream_has_ended()) {
    // If both the contest and the stream is over, destroy the arena.
    gesture_arenas_.erase(stream_id);
  }
}

// The mouse state machine is simpler, comprising MOVE*-DOWN/MOVE*/UP-MOVE*. Its
// behavior is similar to touch events, but with some differences.
//  - There can be multiple mouse devices, so we track each device individually.
//  - Mouse DOWN associates the following DOWN/MOVE*/UP event sequence with one
//    particular client: the top-hit View. Mouse events aren't associated with
//    gestures, so there is no parallel dispatch.
//  - Mouse DOWN triggers a focus change, honoring the "may receive focus" property.
//  - Mouse UP drops the association between event stream and client.
//  - For an unlatched MOVE event, we perform a hit test, and send the
//    top-most client this MOVE event. Focus does not change for unlatched
//    MOVEs.
//  - The hit test must account for the mouse cursor itself, which today is
//    owned by the root presenter. The nodes associated with visible mouse
//    cursors(!) do not roll up to any View (as expected), but may appear in the
//    hit test; our dispatch needs to account for such behavior.
// TODO(fxbug.dev/24288): Enhance trackpad support.
void TouchSystem::LegacyInjectMouseEventHitTested(const InternalTouchEvent& event) {
  const uint32_t device_id = event.device_id;
  const Phase pointer_phase = event.phase;

  if (pointer_phase == Phase::kDown) {
    // Find top-hit target and associated properties.
    const std::vector<zx_koid_t> hit_views =
        hit_tester_.HitTest(event, /*semantic_hit_test*/ false);

    FX_VLOGS(1) << "View hits: ";
    for (auto view_ref_koid : hit_views) {
      FX_VLOGS(1) << "[ViewRefKoid=" << view_ref_koid << "]";
    }

    if (!hit_views.empty()) {
      // Request that focus be transferred to the top view.
      request_focus_(hit_views.front());
    } else {
      // The mouse event stream has no designated receiver.
      // Request that focus be transferred to the root view, so that (1) the currently focused
      // view becomes unfocused, and (2) the focus chain remains under control of the root view.
      request_focus_(ZX_KOID_INVALID);
    }

    // Save target for consistent delivery of mouse events.
    mouse_targets_[device_id] = hit_views;
  }

  if (mouse_targets_.count(device_id) > 0 &&   // Tracking this device, and
      mouse_targets_[device_id].size() > 0) {  // target view exists.
    const zx_koid_t top_view_koid = mouse_targets_[device_id].front();
    ReportPointerEventToGfxLegacyView(event, top_view_koid,
                                      fuchsia::ui::input::PointerEventType::MOUSE);
  }

  if (pointer_phase == Phase::kUp || pointer_phase == Phase::kCancel) {
    mouse_targets_.erase(device_id);
  }

  // Deal with unlatched MOVE events.
  if (pointer_phase == Phase::kChange && mouse_targets_.count(device_id) == 0) {
    // Find top-hit target and send it this move event.
    const zx_koid_t top_view_koid = hit_tester_.TopHitTest(event, /*semantic_hit_test*/ false);
    if (top_view_koid != ZX_KOID_INVALID) {
      ReportPointerEventToGfxLegacyView(event, top_view_koid,
                                        fuchsia::ui::input::PointerEventType::MOUSE);
    }
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

  std::optional<glm::mat4> view_from_context_transform =
      view_tree_snapshot_->GetDestinationViewFromSourceViewTransform(
          /*source*/ event.context, /*destination*/ view_ref_koid);
  if (!view_from_context_transform)
    return;

  const uint64_t trace_id = TRACE_NONCE();
  TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", trace_id);
  InputEvent input_event;
  input_event.set_pointer(InternalPointerEventToGfxPointerEvent(
      event, view_from_context_transform.value(), type, trace_id));
  FX_VLOGS(1) << "Event dispatch to view=" << view_ref_koid << ": " << input_event;
  ChattyGfxLog(input_event);
  contender_inspector_.OnInjectedEvents(view_ref_koid, 1);
  event_reporter->EnqueueEvent(std::move(input_event));
}

}  // namespace scenic_impl::input
