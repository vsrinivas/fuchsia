// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_VIEW_TREE_VIEW_TREE_SNAPSHOTTER_H_
#define SRC_UI_SCENIC_LIB_VIEW_TREE_VIEW_TREE_SNAPSHOTTER_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/ui/lib/glm_workaround/glm_workaround.h"

namespace view_tree {

struct ViewNode {
  zx_koid_t parent = ZX_KOID_INVALID;
  std::unordered_set<zx_koid_t> children = {};

  glm::mat4 local_from_world_transform = glm::mat4(1.f);
  bool focusable = true;

  // TODO(fxbug.dev/72832): This will cause a lot of copies. Investigate if it's a potential
  // performance issue, in which case we might need to store them by shared_ptr or something.
  fuchsia::ui::views::ViewRef view_ref;

  bool operator==(const ViewNode& other) const {
    return parent == other.parent &&
           local_from_world_transform == other.local_from_world_transform &&
           focusable == other.focusable && children == other.children;
  }
};

// Snapshot of a ViewTree.
// When part of a SubtreeSnapshot struct it may represent only a subtree, but otherwise must
// represent be a complete tree.
struct Snapshot {
  // The root of the tree. Must be present in |view_tree|.
  zx_koid_t root = ZX_KOID_INVALID;
  // The |view_tree| must be fully connected and only contain nodes reachable from |root|.
  // Cannot contain ViewNodes with dangling children unless part of a SubtreeSnapshot.
  std::unordered_map<zx_koid_t, ViewNode> view_tree;
  // Must be fully disjoint from |view_tree|.
  std::unordered_set<zx_koid_t> unconnected_views;

  // TODO(fxbug.dev/72077): Add hit testing closures.

  bool operator==(const Snapshot& other) const {
    return root == other.root && view_tree == other.view_tree &&
           unconnected_views == other.unconnected_views;
  }
};

// Representation of a ViewTree subtree.
struct SubtreeSnapshot {
  // The Snapshot of the subtree. No nodes in this subtree may exist in any other subtree.
  // Is allowed to have dangling child pointers to other subtrees, but they must be keys in
  // |tree_boundaries|.
  Snapshot snapshot;
  // Map of leaf nodes in this subtree to their children in other subtrees. Keys must be dangling
  // children in |snapshot| and values must be roots in other subtrees.
  std::multimap<zx_koid_t, zx_koid_t> tree_boundaries;
};

using SubtreeSnapshotGenerator = fit::function<std::vector<SubtreeSnapshot>()>;
using OnNewViewTree = fit::function<void(std::shared_ptr<const Snapshot>)>;

struct Subscriber {
  // |on_new_view_tree| will fire on |dispatcher|.
  // |on_new_view_tree| must be safe to call repeatedly for the lifetime of ViewTreeSnapshotter or
  // |dispatcher|.
  OnNewViewTree on_new_view_tree;
  async_dispatcher_t* dispatcher;
};

// Class for building and handing out snapshots of a ViewTree out of subtrees.
class ViewTreeSnapshotter final {
 public:
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

  // Returns a string representation of |snapshot|.
  // For debugging only.
  static std::string ToString(const Snapshot& snapshot);

 private:
  const std::vector<SubtreeSnapshotGenerator> subtree_generators_;
  std::vector<OnNewViewTree> subscriber_callbacks_;
};

}  // namespace view_tree

#endif  // SRC_UI_SCENIC_LIB_VIEW_TREE_VIEW_TREE_SNAPSHOTTER_H_
