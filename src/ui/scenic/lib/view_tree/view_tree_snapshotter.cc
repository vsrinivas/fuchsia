// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/view_tree_snapshotter.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

namespace view_tree {

namespace {

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

bool ValidateSnapshot(const Snapshot& snapshot, bool check_children) {
  const auto& [root, view_tree, unconnected_views] = snapshot;
  FX_CHECK(root != ZX_KOID_INVALID);
  FX_CHECK(view_tree.count(root) != 0);
  FX_CHECK(view_tree.at(root).parent == ZX_KOID_INVALID);

  size_t tree_walk_size = 0;
  TreeWalk(view_tree, root,
           [&tree_walk_size](zx_koid_t koid, const ViewNode& node) { ++tree_walk_size; });
  FX_CHECK(tree_walk_size == view_tree.size()) << "ViewTree is not fully connected";

  for (const auto& [koid, node] : view_tree) {
    FX_CHECK(unconnected_views.count(koid) == 0)
        << "Node " << koid << " was in both the ViewTree and the unconnected nodes set";

    // Children should be ignored for subtree snapshots.
    if (check_children) {
      for (auto child : node.children) {
        FX_CHECK(view_tree.count(child) != 0)
            << "Child " << child << " of node " << koid << " is not part of the ViewTree";

        FX_CHECK(view_tree.at(child).parent == koid)
            << "Node " << koid << " has child " << child << ", but its parent pointer is "
            << view_tree.at(child).parent;
      }
    }
  }

  return true;
}

bool ValidateSubtree(const SubtreeSnapshot& subtree) {
  const auto& [snapshot, tree_boundaries] = subtree;
  ValidateSnapshot(snapshot, /*check_children*/ false);

  for (const auto& [parent, child] : tree_boundaries) {
    FX_CHECK(snapshot.view_tree.count(parent) != 0)
        << "Parent " << parent << " in tree_boundaries does not exist in the same subtree";
    FX_CHECK(snapshot.view_tree.count(child) == 0)
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
    // Create a std::shared_ptr out of |subscriber_callback| to ensure lifetime of the closure
    // across threads.
    subscriber_callbacks_.emplace_back(
        [dispatcher = dispatcher,
         callback = std::make_shared<OnNewViewTree>(std::move(subscriber_callback))](
            std::shared_ptr<const Snapshot> snapshot) {
          // Queue the closure on |dispatcher|.
          async::PostTask(dispatcher, [callback, snapshot = std::move(snapshot)] {
            (*callback)(std::move(snapshot));
          });
        });
  }
}

void ViewTreeSnapshotter::UpdateSnapshot() const {
  auto new_snapshot = std::make_shared<Snapshot>();
  std::multimap<zx_koid_t, zx_koid_t> tree_boundaries;

  // Merge subtrees.
  for (auto& generate_subtrees : subtree_generators_) {
    for (auto& subtree : generate_subtrees()) {
      FX_DCHECK(ValidateSubtree(subtree));
      auto& [root, view_tree, unconnected_views] = subtree.snapshot;

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
        const size_t subtree_boundaries_size = subtree.tree_boundaries.size();
        tree_boundaries.merge(subtree.tree_boundaries);
        FX_DCHECK(tree_boundaries.size() == boundaries_size_before + subtree_boundaries_size)
            << "Two subtrees had duplicate tree boundaries";
      }
    }
  }

  // Fix parent pointers at subtree boundaries.
  for (const auto& [parent, child] : tree_boundaries) {
    FX_DCHECK(new_snapshot->view_tree.count(parent) != 0);
    FX_DCHECK(new_snapshot->view_tree.count(child) != 0);
    new_snapshot->view_tree.at(child).parent = parent;
  }

  FX_DCHECK(ValidateSnapshot(*new_snapshot, /*check_children*/ true));

  // Update all subscribers with the new snapshot.
  for (const auto& subscriber_callback : subscriber_callbacks_) {
    subscriber_callback(new_snapshot);
  }
}

std::string ViewTreeSnapshotter::ToString(const Snapshot& snapshot) {
  const std::string indent = "  ";
  std::string output = "Root: " + std::to_string(snapshot.root) + "\nViewTree:\n";
  for (const auto& [koid, node] : snapshot.view_tree) {
    output += "[\n";
    output += indent + "koid: " + std::to_string(koid) + "\n";
    output += indent + "ViewNode: [\n";
    output += indent + indent + "parent: " + std::to_string(node.parent) + "\n";
    output += indent + indent + "children: { ";
    for (auto child : node.children) {
      output += std::to_string(child) + " ";
    }
    output += "}\n";
    output += indent + indent + "local_from_world_transform: \n";
    output += indent + indent + indent + std::to_string(node.local_from_world_transform[0][0]) +
              " " + std::to_string(node.local_from_world_transform[0][1]) + " " +
              std::to_string(node.local_from_world_transform[0][2]) + "\n";
    output += indent + indent + indent + std::to_string(node.local_from_world_transform[1][0]) +
              " " + std::to_string(node.local_from_world_transform[1][1]) + " " +
              std::to_string(node.local_from_world_transform[1][2]) + "\n";
    output += indent + indent + indent + std::to_string(node.local_from_world_transform[2][0]) +
              " " + std::to_string(node.local_from_world_transform[2][1]) + " " +
              std::to_string(node.local_from_world_transform[2][2]) + "\n";
    output += indent + indent + "focusable: ";
    if (node.focusable) {
      output += "true\n";
    } else {
      output += "false\n";
    }
    output += indent + "]\n]\n";
  }

  return output;
}

}  // namespace view_tree
