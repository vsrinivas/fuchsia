// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/view_tree_updater.h"

#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"

namespace scenic_impl::gfx {

ViewTreeUpdater::ViewTreeUpdater(SessionId session_id)
    : session_id_(session_id), weak_factory_(this) {}

void ViewTreeUpdater::AddUpdate(ViewTreeUpdate update) {
  return view_tree_updates_.push_back(std::move(update));
}

void ViewTreeUpdater::TrackViewHolder(fxl::WeakPtr<ViewHolder> view_holder) {
  FX_DCHECK(view_holder) << "precondition";  // Called in ViewHolder constructor.

  const zx_koid_t koid = view_holder->view_holder_koid();
  view_tree_updates_.push_back(ViewTreeNewAttachNode{.koid = koid});
  auto [iter, inserted] =
      tracked_view_holders_.insert({koid, ViewHolderStatus{.view_holder = std::move(view_holder)}});
  FX_DCHECK(inserted);
}

void ViewTreeUpdater::UntrackViewHolder(zx_koid_t koid) {
  // Disconnection in view tree handled by DeleteNode operation.
  view_tree_updates_.push_back(ViewTreeDeleteNode{.koid = koid});
  auto erased_count = tracked_view_holders_.erase(koid);
  FX_DCHECK(erased_count == 1);
}

void ViewTreeUpdater::UpdateViewHolderConnections() {
  for (auto& kv : tracked_view_holders_) {
    const zx_koid_t koid = kv.first;
    ViewHolderStatus& status = kv.second;
    const std::optional<bool> prev_connected = status.connected_to_session_root;

    // Each ViewHolder may have an independent intra-Session "root" that connects it upwards.
    // E.g., it's legal to have multiple Scene roots connecting to independent compositors.
    zx_koid_t root = ZX_KOID_INVALID;
    // Determine whether each ViewHolder is connected to some root.
    bool now_connected = false;
    FX_DCHECK(status.view_holder) << "invariant";
    Node* curr = status.view_holder ? status.view_holder->parent() : nullptr;
    while (curr) {
      if (curr->session_id() != session_id_) {
        break;  // Exited session boundary, quit upwards search.
      }
      if (curr->IsKindOf<ViewNode>() && curr->As<ViewNode>()->GetView()) {
        root = curr->As<ViewNode>()->GetView()->view_ref_koid();
        FX_DCHECK(root != ZX_KOID_INVALID) << "invariant";
        // TODO(fxbug.dev/24450): Enable following check when one-view-per-session is enforced.
        // FX_DCHECK(root_view_ && root_view_->view_ref_koid() == root)
        //    << "invariant: session's root-view-discovered and root-view-purported must match.";
        now_connected = true;
        break;
      }
      if (curr->IsKindOf<Scene>()) {
        root = curr->As<Scene>()->view_ref_koid();
        FX_DCHECK(root != ZX_KOID_INVALID) << "invariant";
        now_connected = true;
        break;
      }
      curr = curr->parent();
    }

    // <prev>   <now>   <action>
    // none     true    record connect, report connect (case 1)
    // none     false   record disconnect (case 2)
    // true     true    (nop)
    // true     false   record disconnect, report disconnect (case 3)
    // false    true    record connect, report connect (case 1)
    // false    false   (nop)
    if ((!prev_connected.has_value() && now_connected) ||
        (prev_connected.has_value() && !prev_connected.value() && now_connected)) {
      // Case 1
      status.connected_to_session_root = std::make_optional<bool>(true);
      view_tree_updates_.push_back(ViewTreeConnectToParent{.child = koid, .parent = root});
    } else if (!prev_connected.has_value() && !now_connected) {
      // Case 2
      status.connected_to_session_root = std::make_optional<bool>(false);
    } else if (prev_connected.has_value() && prev_connected.value() && !now_connected) {
      // Case 3
      status.connected_to_session_root = std::make_optional<bool>(false);
      view_tree_updates_.push_back(ViewTreeDisconnectFromParent{.koid = koid});
    }
  }
}

void ViewTreeUpdater::StageViewTreeUpdates(SceneGraph* scene_graph) {
  scene_graph->StageViewTreeUpdates(std::move(view_tree_updates_));
  view_tree_updates_.clear();
}

}  // namespace scenic_impl::gfx
