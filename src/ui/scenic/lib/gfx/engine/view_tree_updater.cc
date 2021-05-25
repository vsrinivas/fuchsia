// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/view_tree_updater.h"

#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"

namespace scenic_impl::gfx {

ViewTreeUpdater::ViewTreeUpdater() : weak_factory_(this) {}

void ViewTreeUpdater::AddUpdate(SessionId session_id, ViewTreeUpdate update) {
  return view_tree_updates_.push_back(std::move(update));
}

void ViewTreeUpdater::TrackViewHolder(SessionId session_id, fxl::WeakPtr<ViewHolder> view_holder) {
  FX_DCHECK(view_holder) << "precondition";  // Called in ViewHolder constructor.

  const zx_koid_t koid = view_holder->view_holder_koid();
  view_tree_updates_.push_back(ViewTreeNewAttachNode{.koid = koid});
  auto [iter, inserted] = tracked_view_holders_.insert(
      {koid, ViewHolderStatus{.session_id = session_id, .view_holder = std::move(view_holder)}});
  FX_DCHECK(inserted);
}

void ViewTreeUpdater::UntrackViewHolder(zx_koid_t koid) {
  // Disconnection in view tree handled by DeleteNode operation.
  view_tree_updates_.push_back(ViewTreeDeleteNode{.koid = koid});
  auto erased_count = tracked_view_holders_.erase(koid);
  FX_DCHECK(erased_count == 1);
}

void ViewTreeUpdater::UpdateViewHolderConnections() {
  for (auto& [koid, status] : tracked_view_holders_) {
    // Each ViewHolder may have an independent intra-Session "root" that connects it upwards.
    // E.g., it's legal to have multiple Scene roots connecting to independent compositors.
    zx_koid_t root = ZX_KOID_INVALID;
    // Determine whether each ViewHolder is connected to some root.
    bool now_connected = false;
    FX_DCHECK(status.view_holder) << "invariant";
    Node* curr = status.view_holder ? status.view_holder->parent() : nullptr;
    while (curr) {
      if (curr->session_id() != status.session_id) {
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
    // true     true    record connect (case 1. Report redundantly, for reparenting case.)
    // true     false   record disconnect (case 2)
    // false    true    record connect (case 1)
    // false    false   (nop)
    const bool prev_connected = status.connected_to_session_root;
    status.connected_to_session_root = now_connected;
    if (now_connected) {
      // Case 1
      view_tree_updates_.push_back(ViewTreeConnectToParent{.child = koid, .parent = root});
    } else if (prev_connected) {
      // Case 2
      view_tree_updates_.push_back(ViewTreeDisconnectFromParent{.koid = koid});
    }
  }
}

ViewTreeUpdates ViewTreeUpdater::FinishAndExtractViewTreeUpdates() {
  UpdateViewHolderConnections();
  auto updates = std::move(view_tree_updates_);
  view_tree_updates_.clear();
  return updates;
}

}  // namespace scenic_impl::gfx
