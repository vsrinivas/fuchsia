// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_VIEW_TREE_VIEW_TREE_SNAPSHOTTER_H_
#define SRC_UI_SCENIC_LIB_VIEW_TREE_VIEW_TREE_SNAPSHOTTER_H_

#include <lib/async/dispatcher.h>

#include <vector>

#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace view_tree {

using SubtreeSnapshotGenerator = fit::function<SubtreeSnapshot()>;

// Class for building and handing out snapshots of a ViewTree out of subtrees.
// All calls to ViewTreeSnapshotter must be made on the same thread.
class ViewTreeSnapshotter final {
 public:
  struct Subscriber {
    // |on_new_view_tree| will run on |dispatcher|.
    // |on_new_view_tree| must be safe to call repeatedly for the lifetime of ViewTreeSnapshotter
    OnNewViewTree on_new_view_tree;
    // |dispatcher| must outlive ViewTreeSnapshotter.
    async_dispatcher_t* dispatcher = nullptr;
  };

  // Each element in |subtrees| will be called once for every call to UpdateSnapshot(). Each
  // closure may generate any number of SubtreeSnapshots with any connectivity, but the very first
  // received SubtreeSnapshots from the first SubtreeSnapshotGenerator *must* be the root of the
  // full ViewTree and the combined set of all SubtreeSnapshots from all |subtrees| *must*
  // constitute a fully connected ViewTree.
  // Each element in |subtrees| must be safe to call repeatedly on this thread for
  // the lifetime of ViewTreeSnapshotter.
  //
  // The |on_new_view_tree| closure of each subscriber in |subscribers| will be called at the
  // end of every UpdateSnapshot() call with the new snapshot, on their individual dispatcher.
  // All subscriber callbacks must be safe to queue on their dispatchers for the lifetime of
  // ViewTreeSnapshotter.
  explicit ViewTreeSnapshotter(std::vector<SubtreeSnapshotGenerator> subtrees,
                               std::vector<Subscriber> subscribers);
  ~ViewTreeSnapshotter() = default;

  // Calls each SubtreeSnapshotGenerator() in |subtree_generators_| in turn, combines the results
  // into a snapshot and hands out the snapshot to each subscriber in |subscriber_callbacks_|.
  void UpdateSnapshot() const;

 private:
  const std::vector<SubtreeSnapshotGenerator> subtree_generators_;
  std::vector<OnNewViewTree> subscriber_callbacks_;
};

}  // namespace view_tree

#endif  // SRC_UI_SCENIC_LIB_VIEW_TREE_VIEW_TREE_SNAPSHOTTER_H_
