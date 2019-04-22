// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_manager/semantics/semantic_tree_impl.h"

#include <lib/fsl/handles/object_info.h>
#include <lib/syslog/cpp/logger.h>
#include <src/lib/fxl/logging.h>

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

bool SemanticTreeImpl::IsSameView(const fuchsia::ui::views::ViewRef& view_ref) {
  return GetKoid(view_ref) == GetKoid(view_ref_);
}

void SemanticTreeImpl::Commit() {
  // TODO(MI4-2038): Commit should ensure that there is only 1 tree rooted at
  // node_id 0.

  // Apply transactions in the order in which they have arrived.
  for (auto& transaction : pending_transactions_) {
    if (transaction.delete_node) {
      DeleteSubtree(transaction.node_id);
      DeletePointerFromParent(transaction.node_id);
    } else {
      // Update Node.
      nodes_[transaction.node_id] = std::move(transaction.node);
    }
  }
  // Clear list of pending transactions.
  pending_transactions_.clear();

  // Get root node of the tree after all the updates/commits are applied.
  fuchsia::accessibility::semantics::NodePtr root_node =
      GetAccessibilityNode(kRootNode);
  if (!root_node) {
    FX_LOGS(ERROR) << "No root node found after applying commit for view(koid):"
                   << GetKoid(view_ref_);
    nodes_.clear();
    return;
  }

  // A tree must be acyclic. Delete if cycle found.
  std::unordered_set<uint32_t> visited;
  if (IsCyclic(std::move(root_node), &visited)) {
    FX_LOGS(ERROR) << "Cycle found in semantic tree with View Id:"
                   << GetKoid(view_ref_);
    nodes_.clear();
  }
}

void SemanticTreeImpl::UpdateSemanticNodes(
    std::vector<fuchsia::accessibility::semantics::Node> nodes) {
  for (auto& node : nodes) {
    SemanticTreeTransaction transaction;
    transaction.delete_node = false;
    transaction.node_id = node.node_id();
    transaction.node = std::move(node);
    pending_transactions_.push_back(std::move(transaction));
  }
}

void SemanticTreeImpl::DeleteSemanticNodes(std::vector<uint32_t> node_ids) {
  for (const auto& node_id : node_ids) {
    SemanticTreeTransaction transaction;
    transaction.delete_node = true;
    transaction.node_id = node_id;
    pending_transactions_.push_back(std::move(transaction));
  }
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
  absl::StrAppend(
      tree_log, "Node_id: ", std::to_string(root_node->node_id()), ", Label:",
      root_node->attributes().has_label() ? root_node->attributes().label()
                                          : "_empty",
      kNewLine);

  // Iterate through all the children of the current node.
  if (!root_node->has_child_ids()) {
    return;
  }
  for (const auto& child : root_node->child_ids()) {
    fuchsia::accessibility::semantics::NodePtr node_ptr =
        GetAccessibilityNode(child);
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

bool SemanticTreeImpl::IsCyclic(fuchsia::accessibility::semantics::NodePtr node,
                                std::unordered_set<uint32_t>* visited) {
  FXL_CHECK(node);
  FXL_CHECK(visited);

  if (visited->count(node->node_id()) > 0) {
    // Cycle Found.
    return true;
  }
  visited->insert(node->node_id());

  if (!node->has_child_ids()) {
    return false;
  }
  for (const auto& child : node->child_ids()) {
    fuchsia::accessibility::semantics::NodePtr child_ptr =
        GetAccessibilityNode(child);
    if (!child_ptr) {
      FX_LOGS(ERROR) << "Child Node(id:" << child
                     << ") not found in the semantic tree for View(koid):"
                     << GetKoid(view_ref_);
      continue;
    }
    if (IsCyclic(std::move(child_ptr), visited))
      return true;
  }
  return false;
}

void SemanticTreeImpl::DeleteSubtree(uint32_t node_id) {
  // Recursively delete the entire subtree at given node_id.
  fuchsia::accessibility::semantics::NodePtr node =
      GetAccessibilityNode(node_id);
  if (!node) {
    return;
  }

  if (node->has_child_ids()) {
    for (const auto& child : node->child_ids()) {
      DeleteSubtree(child);
    }
  }
  nodes_.erase(node_id);
}

void SemanticTreeImpl::DeletePointerFromParent(uint32_t node_id) {
  // Assumption: There is only 1 parent per node.
  // In future, we would like to delete trees not rooted at root node.
  // Loop through all the nodes in the tree, since there can be trees not rooted
  // at 0(root-node).
  for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
    if (it->second.has_child_ids()) {
      // Loop through all the children of the node.
      for (auto child_it = it->second.child_ids().begin();
           child_it != it->second.child_ids().end(); ++child_it) {
        // If a child node is same as node_id, then delete child node from the
        // list.
        if (*child_it == node_id) {
          it->second.mutable_child_ids()->erase(child_it);
          return;
        }
      }
    }
  }
}

}  // namespace a11y_manager
