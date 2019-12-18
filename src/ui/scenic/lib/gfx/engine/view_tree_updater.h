// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_UPDATER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_UPDATER_H_

#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"
#include "src/ui/scenic/lib/gfx/engine/view_tree.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace scenic_impl::gfx {

class ViewHolder;

// Used to track accumulated ViewHolder/ViewRef updates.
class ViewTreeUpdater {
 public:
  ViewTreeUpdater(SessionId session_id);

  void AddUpdate(ViewTreeUpdate update);

  const fxl::WeakPtr<ViewTreeUpdater> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  friend class ViewHolder;
  void TrackViewHolder(fxl::WeakPtr<ViewHolder> view_holder);
  void UntrackViewHolder(zx_koid_t koid);

  friend class Session;
  void UpdateViewHolderConnections();

  // Notify SceneGraph about accumulated ViewHolder/ViewRef updates, but do not apply them yet.
  void StageViewTreeUpdates(SceneGraph* scene_graph);

  struct ViewHolderStatus {
    fxl::WeakPtr<ViewHolder> view_holder;
    // Three cases:
    // - std::nullopt: connectivity unknown
    // - true: connected to session's root (either a View or a Scene).
    // - false: not connected to session's root.
    std::optional<bool> connected_to_session_root;
  };

  // Id for associated Session.
  const scheduling::SessionId session_id_ = 0u;

  // Map of Session's "live" ViewHolder objects that tracks "session root" connectivity.
  std::unordered_map<zx_koid_t, ViewHolderStatus> tracked_view_holders_;

  // Sequentially ordered updates for ViewRef and ViewHolder objects in this Session.
  // Actively maintained over a session update.
  ViewTreeUpdates view_tree_updates_;

  fxl::WeakPtrFactory<ViewTreeUpdater> weak_factory_;  // must be last
};

}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_UPDATER_H_
