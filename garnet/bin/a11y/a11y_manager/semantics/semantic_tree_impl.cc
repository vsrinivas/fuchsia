// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fsl/handles/object_info.h>
#include <lib/syslog/cpp/logger.h>
#include <src/lib/fxl/logging.h>

#include "garnet/bin/a11y/a11y_manager/semantics/semantic_tree_impl.h"

#include "third_party/abseil-cpp/absl/strings/str_cat.h"

namespace a11y_manager {
const std::string kNewLine = "\n";
const std::string::size_type kIndentSize = 4;
const int kRootNode = 0;

void SemanticTreeImpl::OnAccessibilityActionRequested(
    uint32_t node_id, fuchsia::accessibility::semantics::Action action,
    fuchsia::accessibility::semantics::SemanticActionListener::
        OnAccessibilityActionRequestedCallback callback) {
  client_action_listener_->OnAccessibilityActionRequested(node_id, action,
                                                          std::move(callback));
}

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
      scenic_impl::gfx::Unwrap(it->second.data().transform());
  escher::vec4 local_coordinates = transform * coordinates;
  escher::vec2 point(local_coordinates[0], local_coordinates[1]);

  if (!BoxContainsPoint(it->second.data().location(), point)) {
    return nullptr;
  }
  for (auto child : it->second.children_hit_test_order()) {
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
    nodes_[node_it.node_id()] = std::move(node_it);
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

// Helper function to traverse semantic tree and create log message.
void SemanticTreeImpl::LogSemanticTreeHelper(
    fuchsia::accessibility::semantics::NodePtr root_node, int current_level,
    std::string* tree_log) {
  if (!root_node) {
    return;
  }

  // Add constant spaces proportional to the current tree level, so that
  // child nodes are indented under parent node.
  tree_log->append(kIndentSize * current_level, ' ');

  // Add logs for the current node.
  absl::StrAppend(tree_log,
                  "Node_id: ", std::to_string(root_node.get()->node_id()),
                  ", Label:", root_node.get()->data().label(), kNewLine);

  // Iterate through all the children of the current node.
  for (auto it = root_node.get()->children_traversal_order().begin();
       it != root_node.get()->children_traversal_order().end(); ++it) {
    fuchsia::accessibility::semantics::NodePtr node_ptr =
        GetAccessibilityNode(*it);
    LogSemanticTreeHelper(std::move(node_ptr), current_level + 1, tree_log);
  }
}

std::string SemanticTreeImpl::LogSemanticTree() {
  // Get the root node.
  fuchsia::accessibility::semantics::NodePtr node_ptr =
      GetAccessibilityNode(kRootNode);
  std::string tree_log;
  if (!node_ptr) {
    tree_log = "Root Node not found.";
    FX_LOGS(ERROR) << tree_log;
    return tree_log;
  }

  // Start with the root node(i.e: Node - 0).
  LogSemanticTreeHelper(std::move(node_ptr), kRootNode, &tree_log);
  FX_VLOGS(1) << "Semantic Tree:" << std::endl << tree_log;
  return tree_log;
}

}  // namespace a11y_manager
