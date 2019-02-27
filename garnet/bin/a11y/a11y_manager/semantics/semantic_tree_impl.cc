// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "semantic_tree_impl.h"

namespace a11y_manager {

const int kRootNode = 0;

// Internal helper function to check if a point is within a bounding box.
bool SemanticTreeImpl::BoxContainsPoint(
    const fuchsia::ui::gfx::BoundingBox& box, const escher::vec2& point) const {
  return box.min.x <= point.x && box.max.x >= point.x && box.min.y <= point.y &&
         box.max.y >= point.y;
}

const fuchsia::accessibility::semantics::NodePtr SemanticTreeImpl::HitTest(
    const std::unordered_map<uint32_t, fuchsia::accessibility::semantics::Node>&
        nodes,
    uint32_t starting_node_id, escher::vec4 coordinates) const {
  auto it = nodes.find(starting_node_id);
  if (it == nodes.end()) {
    return nullptr;
  }
  escher::mat4 transform =
      scenic_impl::gfx::Unwrap(*it->second.data()->transform());
  escher::vec4 local_coordinates = transform * coordinates;
  escher::vec2 point(local_coordinates[0], local_coordinates[1]);

  if (!BoxContainsPoint(*it->second.data()->location(), point)) {
    return nullptr;
  }
  for (auto child : *it->second.children_hit_test_order()) {
    auto node = HitTest(nodes, child, local_coordinates);
    if (node != nullptr) {
      return node;
    }
  }
  auto node_ptr = fuchsia::accessibility::semantics::Node::New();
  it->second.Clone(node_ptr.get());
  return node_ptr;
}

fuchsia::accessibility::semantics::NodePtr
SemanticTreeImpl::GetHitAccessibilityNode(fuchsia::math::PointF point) {
  escher::vec4 coordinate(point.x, point.y, 0, 1);
  return HitTest(nodes_, kRootNode, coordinate);
}

fuchsia::accessibility::semantics::NodePtr
SemanticTreeImpl::GetAccessibilityNode(uint32_t node_id) {
  auto node_it = nodes_.find(node_id);
  if (node_it == nodes_.end()) {
    return nullptr;
  }
  auto node_ptr = fuchsia::accessibility::semantics::Node::New();
  node_it->second.Clone(node_ptr.get());
  return node_ptr;
}

void SemanticTreeImpl::Commit() {
  // TODO(MI4-2038): Update Commit method to detect cycles in tree
  // and cleanup dangling subtrees.

  // Commit uncommitted nodes.
  for (auto& node_it : uncommitted_nodes_) {
    nodes_[*node_it.node_id()] = std::move(node_it);
  }
  uncommitted_nodes_.clear();

  // Remove nodes marked for deletion.
  for (auto& node_it : uncommitted_deletes_) {
    nodes_.erase(node_it);
  }
  uncommitted_deletes_.clear();
}

void SemanticTreeImpl::UpdateSemanticNodes(
    std::vector<fuchsia::accessibility::semantics::Node> nodes) {
  // TODO(MI4-2038): Update/Delete should happen in the order in which the
  // request is received.
  uncommitted_nodes_.insert(uncommitted_nodes_.end(),
                            std::make_move_iterator(nodes.begin()),
                            std::make_move_iterator(nodes.end()));
}

void SemanticTreeImpl::DeleteSemanticNodes(std::vector<uint32_t> node_ids) {
  // TODO(MI4-2038): Update/Delete should happen in the order in which the
  // request is received.
  uncommitted_deletes_.insert(uncommitted_deletes_.end(),
                              std::make_move_iterator(node_ids.begin()),
                              std::make_move_iterator(node_ids.end()));
}

}  // namespace a11y_manager
