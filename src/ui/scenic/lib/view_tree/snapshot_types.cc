// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/utils/math.h"

namespace view_tree {

std::vector<zx_koid_t> Snapshot::HitTest(zx_koid_t start_node, glm::vec2 world_space_point,
                                         bool is_semantic) const {
  FX_DCHECK(!hit_testers.empty());
  if (view_tree.count(start_node) == 0) {
    return {};
  }

  SubtreeHitTestResult result;
  const auto view_local_point = utils::TransformPointerCoords(
      world_space_point, view_tree.at(start_node).local_from_world_transform);

  if (hit_testers.empty()) {
    FX_LOGS(WARNING) << "No hit testers found.";
  }

  for (const auto& hit_tester : hit_testers) {
    result = hit_tester(start_node, view_local_point, is_semantic);
    // At most one hit tester should have results for this |start_node|, since each node only
    // exists in one subtree.
    if (!result.hits.empty() || !result.continuations.empty()) {
      break;
    }
  }

  // Perform new hit tests for continuations and insert the subtree hits in between hits from the
  // parent according to the definition of continuations in SubtreeHitTestResult.
  // Example in SnapshotHitTestTest.ContinuationsShouldHonorInsertionOrder.
  auto& [hits, continuations] = result;
  size_t offset = 0;
  const size_t start_size = hits.size();
  for (const auto& [index, koid] : continuations) {
    FX_DCHECK(koid != start_node) << "infinite recursion";
    FX_DCHECK(index <= start_size) << "out of bounds";
    auto subhits = HitTest(koid, world_space_point, is_semantic);
    hits.insert(hits.begin() + offset + index, subhits.begin(), subhits.end());
    offset += subhits.size();
  }

  return hits;
}

bool Snapshot::IsDescendant(zx_koid_t descendant_koid, zx_koid_t ancestor_koid) const {
  if (view_tree.count(descendant_koid) == 0 || view_tree.count(ancestor_koid) == 0) {
    return false;
  }

  zx_koid_t parent_koid = view_tree.at(descendant_koid).parent;
  while (parent_koid != ZX_KOID_INVALID) {
    FX_DCHECK(view_tree.count(parent_koid) != 0);
    if (parent_koid == ancestor_koid) {
      return true;
    }
    parent_koid = view_tree.at(parent_koid).parent;
  }

  return false;
}

std::vector<zx_koid_t> Snapshot::GetAncestorsOf(zx_koid_t koid) const {
  FX_DCHECK(view_tree.count(koid) != 0) << "precondition";
  std::vector<zx_koid_t> ancestors;
  zx_koid_t parent_koid = view_tree.at(koid).parent;
  while (parent_koid != ZX_KOID_INVALID) {
    FX_DCHECK(view_tree.count(parent_koid) != 0);
    ancestors.emplace_back(parent_koid);
    parent_koid = view_tree.at(parent_koid).parent;
  }

  return ancestors;
}

std::ostream& operator<<(std::ostream& os, const ViewNode& node) {
  const std::string indent = "  ";
  os << "[\n";
  os << indent << "ViewNode: [\n";
  os << indent << indent << "parent: " << std::to_string(node.parent) << '\n';
  os << indent << indent << "children: { ";
  for (auto child : node.children) {
    os << std::to_string(child) << " ";
  }
  os << "}\n";
  os << indent << indent;
  os << "local_from_world_transform: \n";
  os << indent << indent << indent << std::to_string(node.local_from_world_transform[0][0]) << " "
     << std::to_string(node.local_from_world_transform[0][1]) << " "
     << std::to_string(node.local_from_world_transform[0][2]) << " "
     << std::to_string(node.local_from_world_transform[0][3]) << "\n";
  os << indent << indent << indent << std::to_string(node.local_from_world_transform[1][0]) << " "
     << std::to_string(node.local_from_world_transform[1][1]) << " "
     << std::to_string(node.local_from_world_transform[1][2]) << " "
     << std::to_string(node.local_from_world_transform[1][3]) << "\n";
  os << indent << indent << indent << std::to_string(node.local_from_world_transform[2][0]) << " "
     << std::to_string(node.local_from_world_transform[2][1]) << " "
     << std::to_string(node.local_from_world_transform[2][2]) << " "
     << std::to_string(node.local_from_world_transform[2][3]) << "\n";
  os << indent << indent << indent << std::to_string(node.local_from_world_transform[3][0]) << " "
     << std::to_string(node.local_from_world_transform[3][1]) << " "
     << std::to_string(node.local_from_world_transform[3][2]) << " "
     << std::to_string(node.local_from_world_transform[3][3]) << "\n";
  os << indent << indent << "is_focusable: ";
  if (node.is_focusable) {
    os << "true\n";
  } else {
    os << "false\n";
  }
  os << indent << "]\n]\n";
  return os;
}

std::ostream& operator<<(std::ostream& os, const Snapshot& snapshot) {
  os << "Root: " << std::to_string(snapshot.root) << "\nViewTree:\n";
  for (const auto& [koid, node] : snapshot.view_tree) {
    os << "koid: " << std::to_string(koid) << "\n";
    os << node;
  }

  return os;
}

}  // namespace view_tree
