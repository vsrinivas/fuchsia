// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/mouse_system.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/ui/scenic/lib/input/internal_pointer_event.h"
#include "src/ui/scenic/lib/input/mouse_source.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/lib/utils/math.h"

#include <glm/glm.hpp>

namespace scenic_impl::input {

MouseSystem::MouseSystem(sys::ComponentContext* context,
                         std::shared_ptr<const view_tree::Snapshot>& view_tree_snapshot,
                         HitTester& hit_tester, fit::function<void(zx_koid_t)> request_focus)
    : view_tree_snapshot_(view_tree_snapshot),
      hit_tester_(hit_tester),
      request_focus_(std::move(request_focus)) {
  context->outgoing()->AddPublicService(global_mouse_upgrade_registry_.GetHandler(this));
}

void MouseSystem::RegisterMouseSource(
    fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource> mouse_source_request,
    zx_koid_t client_view_ref_koid) {
  const auto [it, success] = mouse_sources_.emplace(
      client_view_ref_koid,
      std::make_unique<MouseSource>(std::move(mouse_source_request),
                                    /*error_handler*/ [this, client_view_ref_koid] {
                                      mouse_sources_.erase(client_view_ref_koid);
                                    }));
  FX_DCHECK(success);
}

zx_koid_t MouseSystem::FindViewRefKoidOfRelatedChannel(
    const fidl::InterfaceHandle<fuchsia::ui::pointer::MouseSource>& original) const {
  const zx_koid_t related_koid = utils::ExtractRelatedKoid(original.channel());
  const auto it = std::find_if(
      mouse_sources_.begin(), mouse_sources_.end(),
      [related_koid](const auto& kv) { return kv.second->channel_koid() == related_koid; });
  return it == mouse_sources_.end() ? ZX_KOID_INVALID : it->first;
}

void MouseSystem::Upgrade(fidl::InterfaceHandle<fuchsia::ui::pointer::MouseSource> original,
                          UpgradeCallback callback) {
  // TODO(fxbug.dev/84270): This currently requires the client to wait until the MouseSource has
  // been hooked up before making the Upgrade() call. This is not a great user experience. Change
  // this so we cache the channel if it arrives too early.
  const zx_koid_t view_ref_koid = FindViewRefKoidOfRelatedChannel(original);
  if (view_ref_koid == ZX_KOID_INVALID) {
    auto error = std::make_unique<fuchsia::ui::pointer::augment::ErrorForGlobalMouse>();
    error->error_reason = fuchsia::ui::pointer::augment::ErrorReason::DENIED;
    error->original = std::move(original);
    callback({}, std::move(error));
    return;
  } else {
    mouse_sources_.erase(view_ref_koid);
    fidl::InterfaceHandle<fuchsia::ui::pointer::augment::MouseSourceWithGlobalMouse> handle;
    auto global_mouse =
        std::make_unique<MouseSourceWithGlobalMouse>(handle.NewRequest(),
                                                     /*error_handler*/ [this, view_ref_koid] {
                                                       global_mouse_sources_.erase(view_ref_koid);
                                                       mouse_sources_.erase(view_ref_koid);
                                                     });
    const auto [_, success] = global_mouse_sources_.emplace(view_ref_koid, global_mouse.get());
    FX_DCHECK(success);
    const auto [__, success2] = mouse_sources_.emplace(view_ref_koid, std::move(global_mouse));
    FX_DCHECK(success2);

    callback(std::move(handle), nullptr);
  }
}

void MouseSystem::SendEventToMouse(zx_koid_t receiver, const InternalMouseEvent& event,
                                   const StreamId stream_id, bool view_exit) {
  const auto it = mouse_sources_.find(receiver);
  if (it != mouse_sources_.end()) {
    if (view_exit) {
      // Bounding box and correct transform does not matter on view exit (since we don't send any
      // pointer samples), and we are likely working with a broken ViewTree, so skip them.
      it->second->UpdateStream(stream_id, event, {}, view_exit);
    } else {
      it->second->UpdateStream(
          stream_id, EventWithReceiverFromViewportTransform(event, receiver, *view_tree_snapshot_),
          view_tree_snapshot_->view_tree.at(receiver).bounding_box, view_exit);
    }
  }
}

void MouseSystem::UpdateGlobalMouse(const InternalMouseEvent& event) {
  const auto hits = hit_tester_.HitTest(event, /*semantic_hit_test*/ false);
  for (auto& [koid, mouse] : global_mouse_sources_) {
    FX_DCHECK(koid == event.target ||
              view_tree_snapshot_->IsDescendant(/*descendant*/ koid, /*ancestor*/ event.target));
    const bool inside_view = std::find(hits.begin(), hits.end(), koid) != hits.end();
    mouse->AddGlobalEvent(event, inside_view);
  }
}

void MouseSystem::InjectMouseEventExclusive(const InternalMouseEvent& event,
                                            const StreamId stream_id) {
  FX_DCHECK(view_tree_snapshot_->IsDescendant(event.target, event.context))
      << "Should never allow injection into broken scene graph";
  FX_DCHECK(current_exclusive_mouse_receivers_.count(stream_id) == 0 ||
            current_exclusive_mouse_receivers_.at(stream_id) == event.target);
  current_exclusive_mouse_receivers_[stream_id] = event.target;
  SendEventToMouse(event.target, event, stream_id, /*view_exit=*/false);

  // If the exclusive receiver is a MouseSourceWithGlobalMouse, add the global values to it.
  const auto it = global_mouse_sources_.find(event.target);
  if (it != global_mouse_sources_.end()) {
    const auto hits = hit_tester_.HitTest(event, /*semantic_hit_test*/ false);
    const bool inside_view = std::find(hits.begin(), hits.end(), event.target) != hits.end();
    it->second->AddGlobalEvent(event, inside_view);
  }
}

void MouseSystem::InjectMouseEventHitTested(const InternalMouseEvent& event,
                                            const StreamId stream_id) {
  FX_DCHECK(view_tree_snapshot_->IsDescendant(event.target, event.context))
      << "Should never allow injection into broken scene graph";
  // Grab the current mouse receiver or create a new one.
  MouseReceiver& mouse_receiver = current_mouse_receivers_[stream_id];

  // Unlatch a current latch if all buttons are released.
  const bool button_down = !event.buttons.pressed.empty();
  mouse_receiver.latched = mouse_receiver.latched && button_down;

  // If the scene graph breaks while latched -> send a "View Exited" event and invalidate the
  // receiver for the remainder of the latch.
  if (mouse_receiver.latched &&
      !view_tree_snapshot_->IsDescendant(mouse_receiver.view_koid, event.target) &&
      mouse_receiver.view_koid != event.target) {
    SendEventToMouse(mouse_receiver.view_koid, event, stream_id, /*view_exit=*/true);
    mouse_receiver.view_koid = ZX_KOID_INVALID;
    UpdateGlobalMouse(event);
    return;
  }
  // If not latched, choose the current target by finding the top view.
  if (!mouse_receiver.latched) {
    const zx_koid_t top_koid = hit_tester_.TopHitTest(event, /*semantic_hit_test*/ false);

    // Determine the currently hovered view. If it's different than previously, send the
    // previous one a "View Exited" event.
    if (mouse_receiver.view_koid != top_koid) {
      SendEventToMouse(mouse_receiver.view_koid, event, stream_id, /*view_exit=*/true);
    }
    mouse_receiver.view_koid = top_koid;

    // Button down on an unlatched stream -> latch it to the top-most view.
    if (button_down) {
      mouse_receiver.latched = true;
      request_focus_(mouse_receiver.view_koid);
    }
  }

  // Finally, send the event to the hovered/latched view.
  SendEventToMouse(mouse_receiver.view_koid, event, stream_id, /*view_exit=*/false);

  // Update all MouseSourceWithGlobalMouse.
  UpdateGlobalMouse(event);
}

void MouseSystem::CancelMouseStream(StreamId stream_id) {
  zx_koid_t receiver = ZX_KOID_INVALID;
  {
    const auto it = current_mouse_receivers_.find(stream_id);
    if (it != current_mouse_receivers_.end()) {
      receiver = it->second.view_koid;
      current_mouse_receivers_.erase(it);
    }
  }
  {
    const auto it = current_exclusive_mouse_receivers_.find(stream_id);
    if (it != current_exclusive_mouse_receivers_.end()) {
      receiver = it->second;
      current_exclusive_mouse_receivers_.erase(it);
    }
  }

  const auto it = mouse_sources_.find(receiver);
  if (it != mouse_sources_.end()) {
    it->second->UpdateStream(stream_id, {}, {}, /*view_exit=*/true);
  }
}

}  // namespace scenic_impl::input
