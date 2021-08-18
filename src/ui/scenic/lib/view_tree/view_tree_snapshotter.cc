// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/view_tree_snapshotter.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

namespace view_tree {

namespace {

template <typename K, typename V>
bool ContainsKeyValuePair(const std::multimap<K, V>& multimap, K key, V value) {
  auto key_range = multimap.equal_range(key);
  for (auto it = key_range.first; it != key_range.second; ++it) {
    if (it->second == value) {
      return true;
      break;
    }
  }
  return false;
}

// Recursively walks the tree and calls |visitor| on each node.
// Ignores child pointers without corresponding child nodes.
void TreeWalk(const std::unordered_map<zx_koid_t, ViewNode>& view_tree, zx_koid_t root,
              const fit::function<void(zx_koid_t, const ViewNode&)>& visitor) {
  if (view_tree.count(root) == 0) {
    return;
  }

  visitor(root, view_tree.at(root));

  for (auto child : view_tree.at(root).children) {
    TreeWalk(view_tree, child, visitor);
  }
}

bool ValidateSnapshot(const Snapshot& snapshot) {
  const auto& [root, view_tree, unconnected_views, hit_testers] = snapshot;
  if (view_tree.empty() && root == ZX_KOID_INVALID) {
    return true;
  }

  FX_DCHECK(root != ZX_KOID_INVALID);
  FX_DCHECK(view_tree.count(root) != 0);
  FX_DCHECK(view_tree.at(root).parent == ZX_KOID_INVALID);

  size_t tree_walk_size = 0;
  TreeWalk(view_tree, root,
           [&tree_walk_size](zx_koid_t koid, const ViewNode& node) { ++tree_walk_size; });
  FX_DCHECK(tree_walk_size == view_tree.size()) << "ViewTree is not fully connected";

  for (const auto& [koid, node] : view_tree) {
    FX_DCHECK(unconnected_views.count(koid) == 0)
        << "Node " << koid << " was in both the ViewTree and the unconnected nodes set";
    for (auto child : node.children) {
      FX_DCHECK(view_tree.count(child) != 0)
          << "Child " << child << " of node " << koid << " is not part of the ViewTree";
      FX_DCHECK(view_tree.at(child).parent == koid)
          << "Node " << koid << " has child " << child << ", but its parent pointer is "
          << view_tree.at(child).parent;
    }
  }

  return true;
}

bool ValidateSubtree(const SubtreeSnapshot& subtree) {
  const auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = subtree;
  if (view_tree.empty() && root == ZX_KOID_INVALID) {
    return true;
  }
  FX_DCHECK(root != ZX_KOID_INVALID);
  FX_DCHECK(view_tree.count(root) != 0);
  FX_DCHECK(view_tree.at(root).parent == ZX_KOID_INVALID);

  size_t tree_walk_size = 0;
  TreeWalk(view_tree, root, [&tree_walk_size](zx_koid_t koid, const ViewNode& node) {
    FX_DCHECK(node.view_ref) << "ViewRef not set on node " << koid;
    ++tree_walk_size;
  });
  FX_DCHECK(tree_walk_size == view_tree.size()) << "ViewTree is not fully connected";

  for (const auto& [koid, node] : view_tree) {
    FX_DCHECK(unconnected_views.count(koid) == 0)
        << "Node " << koid << " was in both the ViewTree and the unconnected nodes set";
    for (auto child : node.children) {
      FX_DCHECK(view_tree.count(child) != 0 || ContainsKeyValuePair(tree_boundaries, koid, child))
          << "Child " << child << " of node " << koid
          << " is not part of the ViewTree or tree_boundaries";
      FX_DCHECK(view_tree.count(child) == 0 || view_tree.at(child).parent == koid)
          << "Node " << koid << " has child " << child << ", but its parent pointer is "
          << view_tree.at(child).parent;
    }
  }

  for (const auto& [parent, child] : tree_boundaries) {
    FX_DCHECK(view_tree.count(parent) != 0)
        << "Parent " << parent << " in tree_boundaries does not exist in the same subtree";
    FX_DCHECK(view_tree.count(child) == 0)
        << "Child " << child << " in tree_boundaries should not exist in the same subtree";
  }

  return true;
}

}  // namespace

ViewTreeSnapshotter::ViewTreeSnapshotter(std::vector<SubtreeSnapshotGenerator> subtree_generators,
                                         std::vector<Subscriber> subscribers)
    : subtree_generators_(std::move(subtree_generators)) {
  for (auto& [subscriber_callback, dispatcher] : subscribers) {
    FX_DCHECK(dispatcher);
    // TODO(fxbug.dev/75864): We save the callback directly and ignore the dispatcher as a
    // workaround to avoid flakes. Rework this after deciding on a new synchronization mechanism.
    subscriber_callbacks_.emplace_back(std::move(subscriber_callback));
  }
}

void ViewTreeSnapshotter::UpdateSnapshot() const {
  auto new_snapshot = std::make_shared<Snapshot>();
  std::multimap<zx_koid_t, zx_koid_t> tree_boundaries;

  // Merge subtrees.
  for (auto& generate_subtrees : subtree_generators_) {
    auto subtree = generate_subtrees();
    FX_DCHECK(ValidateSubtree(subtree));
    auto& [root, view_tree, unconnected_views, hit_tester, subtree_boundaries] = subtree;

    if (new_snapshot->root == ZX_KOID_INVALID) {
      new_snapshot->root = root;
    }

    {
      const size_t tree_size_before = new_snapshot->view_tree.size();
      const size_t subtree_size = view_tree.size();
      new_snapshot->view_tree.merge(view_tree);
      FX_DCHECK(new_snapshot->view_tree.size() == tree_size_before + subtree_size)
          << "Two subtrees had duplicate nodes";
    }
    {
      const size_t unconnected_size_before = new_snapshot->unconnected_views.size();
      const size_t subtree_unconnected_size = unconnected_views.size();
      new_snapshot->unconnected_views.merge(unconnected_views);
      FX_DCHECK(new_snapshot->unconnected_views.size() ==
                unconnected_size_before + subtree_unconnected_size)
          << "Two subtrees had duplicate unconnected nodes";
    }

    {
      const size_t boundaries_size_before = tree_boundaries.size();
      const size_t subtree_boundaries_size = subtree_boundaries.size();
      tree_boundaries.merge(subtree_boundaries);
      FX_DCHECK(tree_boundaries.size() == boundaries_size_before + subtree_boundaries_size)
          << "Two subtrees had duplicate tree boundaries";
    }

    if (hit_tester) {
      new_snapshot->hit_testers.emplace_back(std::move(hit_tester));
    }
  }

  // Fix parent pointers at subtree boundaries.
  for (const auto& [parent, child] : tree_boundaries) {
    FX_DCHECK(new_snapshot->view_tree.count(parent) != 0);
    FX_DCHECK(new_snapshot->view_tree.count(child) != 0);
    new_snapshot->view_tree.at(child).parent = parent;
  }

  FX_DCHECK(ValidateSnapshot(*new_snapshot));

  // Update all subscribers with the new snapshot.
  for (const auto& subscriber_callback : subscriber_callbacks_) {
    subscriber_callback(new_snapshot);
  }
}

}  // namespace view_tree
