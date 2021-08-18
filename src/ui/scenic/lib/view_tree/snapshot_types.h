// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_VIEW_TREE_SNAPSHOT_TYPES_H_
#define SRC_UI_SCENIC_LIB_VIEW_TREE_SNAPSHOT_TYPES_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

#include <array>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/ui/lib/glm_workaround/glm_workaround.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace view_tree {

class Snapshot;
struct SubtreeHitTestResult;

using OnNewViewTree = fit::function<void(std::shared_ptr<const Snapshot>)>;
// Hit tester for a subtree.
// |start_node| defines the root of the hit testing tree walk. It can be any arbitrary node.
// |is_semantic_hit_test| defines if this is a semantic hit test, and this if it should follow the
// special accessibility hit testing rules or not.
using SubtreeHitTester = fit::function<SubtreeHitTestResult(
    zx_koid_t start_node, glm::vec2 local_point, bool is_semantic_hit_test)>;

struct BoundingBox {
  std::array<float, 2> min;
  std::array<float, 2> max;
  // Used to detect changes. The bounding box is defined in local space and not affected by
  // any transformations, so exact matching is preferred. No need for an epsilon comparison.
  bool operator==(const BoundingBox& other) const { return min == other.min && max == other.max; }
  bool operator!=(const BoundingBox& other) const { return !(*this == other); }
};

// Represents an element in a View hierarchy, used in both Snapshot and SubtreeSnapshot (both of
// which are defined below).
struct ViewNode {
  zx_koid_t parent = ZX_KOID_INVALID;
  std::unordered_set<zx_koid_t> children = {};

  BoundingBox bounding_box;
  glm::mat4 local_from_world_transform = glm::mat4(1.f);
  bool is_focusable = true;

  std::shared_ptr<const fuchsia::ui::views::ViewRef> view_ref = nullptr;

  bool operator==(const ViewNode& other) const {
    return parent == other.parent && bounding_box == other.bounding_box &&
           local_from_world_transform == other.local_from_world_transform &&
           is_focusable == other.is_focusable && children == other.children &&
           ((!view_ref && !other.view_ref) ||
            utils::ExtractKoid(*view_ref) == utils::ExtractKoid(*other.view_ref));
  }
};

// The results of a hit test from a single SubtreeHitTester.
struct SubtreeHitTestResult {
  // Hit views in order of increasing distance.
  // The hits are scoped to this subtree, and do not contain hits from continuations.
  std::vector<zx_koid_t> hits;

  // Views to continue hit testing from (in other subtrees), and the position in |hits| to insert
  // the subtree hits in front of. If multiple continuations have the same index then they maintain
  // their ordering in the multimap (insertion order).
  // Assumption: An embedded subtree is rendered to a quad, i.e. all points in the subtree are in
  // the same 2D dimensional plane in the parent tree; so all hits in a subtree are at the same
  // distance from the parent tree's point of view.
  std::multimap</*index*/ size_t, zx_koid_t> continuations;
};

// Output of ViewTreeSnapshotter.
// Snapshot of a ViewTree
class Snapshot {
 public:
  // The root of the tree. Must be present in |view_tree|.
  zx_koid_t root = ZX_KOID_INVALID;
  // The |view_tree| must be fully connected and only contain nodes reachable from |root|.
  // Cannot contain ViewNodes with dangling children.
  std::unordered_map<zx_koid_t, ViewNode> view_tree;
  // Must be fully disjoint from |view_tree|.
  std::unordered_set<zx_koid_t> unconnected_views;

  // List of hit testers provided by all subtrees. When performing hit tests each hit tester is
  // tried until a hit is found. This lets each subtree have customized hit testing.
  // It's legal for one subtree to supply multiple hit testers.
  // No one should be querying |hit_testers| directly. Only through view_tree::HitTest().
  std::vector<SubtreeHitTester> hit_testers;

  // Perform a hit test on |snapshot| starting from |start_node|.
  // Returns a list of hit views in order of increasing distance.
  // Recursively called on each tree boundary, so that the result is a full traversal of the tree.
  // |start_node| defines the root of the hit testing tree walk. It can be any arbitrary node.
  // |is_semantic| defines if this is a semantic hit test, and this if it should follow the special
  // accessibility hit testing rules or not.
  //
  // Complexity is O(number of subtrees * O(hit_testers))
  std::vector<zx_koid_t> HitTest(zx_koid_t start_node, glm::vec2 world_space_point,
                                 bool is_semantic) const;

  // Given a node's KOID, return true if it transitively connects to node with |ancestor_koid| via
  // parent references.
  // - This operation is O(N) in the depth of the view tree.
  bool IsDescendant(zx_koid_t descendant_koid, zx_koid_t ancestor_koid) const;

  // Given a node's koid, return the list of all ancestors, ordered from closest to most distant.
  // Precondition: |koid| must exist in the |view_tree|.
  std::vector<zx_koid_t> GetAncestorsOf(zx_koid_t koid) const;

  bool operator==(const Snapshot& other) const {
    return root == other.root && view_tree == other.view_tree &&
           unconnected_views == other.unconnected_views;
  }
};

// Input to ViewTreeSnapshotter.
// Representation of a ViewTree subtree.
struct SubtreeSnapshot {
  // The root of the tree. Must be present in |view_tree|.
  zx_koid_t root = ZX_KOID_INVALID;
  // The |view_tree| must be fully connected and only contain nodes reachable from |root|.
  // May contain ViewNodes with dangling children.
  std::unordered_map<zx_koid_t, ViewNode> view_tree;
  // Must be fully disjoint from |view_tree|.
  std::unordered_set<zx_koid_t> unconnected_views;

  // Hit tester for this subtree.
  SubtreeHitTester hit_tester = nullptr;

  // Map of leaf nodes in this subtree to their children in other subtrees. Keys must be dangling
  // children in |view_tree| and values must be roots in other subtrees.
  // Multimap so that one key can have multiple children.
  std::multimap<zx_koid_t, zx_koid_t> tree_boundaries;
};

std::ostream& operator<<(std::ostream& os, const ViewNode& node);
std::ostream& operator<<(std::ostream& os, const Snapshot& snapshot);

}  // namespace view_tree

#endif  // SRC_UI_SCENIC_LIB_VIEW_TREE_SNAPSHOT_TYPES_H_
