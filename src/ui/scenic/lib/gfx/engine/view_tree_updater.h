// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_UPDATER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_UPDATER_H_

#include "src/ui/scenic/lib/gfx/engine/view_tree.h"

namespace scenic_impl::gfx {

class ViewHolder;

// Used to track accumulated ViewHolder/ViewRef updates.
class ViewTreeUpdater {
 public:
  ViewTreeUpdater();

  void AddUpdate(SessionId session_id, ViewTreeUpdate update);

  // Updates ViewHolder connections, returns pending updates and then clears |view_tree_updates_|.
  ViewTreeUpdates FinishAndExtractViewTreeUpdates();

  const fxl::WeakPtr<ViewTreeUpdater> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  friend class ViewHolder;
  void TrackViewHolder(SessionId session_id, fxl::WeakPtr<ViewHolder> view_holder);
  void UntrackViewHolder(zx_koid_t koid);

  void UpdateViewHolderConnections();

  struct ViewHolderStatus {
    const SessionId session_id;
    const fxl::WeakPtr<ViewHolder> view_holder;
    // Whether connected to session's root (either a View or a Scene).
    bool connected_to_session_root = false;
  };

  // Map of "live" ViewHolder objects that tracks "session root" connectivity.
  std::unordered_map<zx_koid_t, ViewHolderStatus> tracked_view_holders_;

  // Sequentially ordered updates for ViewRef and ViewHolder objects.
  // Actively maintained over one SessionUpdater::UpdateSessions() call.
  ViewTreeUpdates view_tree_updates_;

  fxl::WeakPtrFactory<ViewTreeUpdater> weak_factory_;  // must be last
};

}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_UPDATER_H_
