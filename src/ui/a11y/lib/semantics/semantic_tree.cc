// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/semantic_tree.h"

#include <lib/fsl/handles/object_info.h>
#include <lib/syslog/cpp/logger.h>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "zircon/third_party/uapp/dash/src/output.h"

namespace a11y {

namespace {

// Max file size of semantic tree log file is 1MB.
constexpr size_t kMaxDebugFileSize = 1024 * 1024;
const std::string kNewLine = "\n";
constexpr std::string::size_type kIndentSize = 4;
constexpr int kRootNode = 0;

}  // namespace

SemanticTree::SemanticTree(fuchsia::ui::views::ViewRef view_ref,
                           fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
                           vfs::PseudoDir* debug_dir, CommitErrorCallback commit_error_callback)
    : commit_error_callback_(std::move(commit_error_callback)),
      view_ref_(std::move(view_ref)),
      semantic_listener_(std::move(semantic_listener)),
      debug_dir_(debug_dir) {
  InitializeDebugEntry();
}

SemanticTree::~SemanticTree() = default;

void SemanticTree::OnAccessibilityActionRequested(
    uint32_t node_id, fuchsia::accessibility::semantics::Action action,
    fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
        callback) {
  semantic_listener_->OnAccessibilityActionRequested(node_id, action, std::move(callback));
}

// Internal helper function to check if a point is within a bounding box.
bool SemanticTree::BoxContainsPoint(const fuchsia::ui::gfx::BoundingBox& box,
                                    const escher::vec2& point) {
  return box.min.x <= point.x && box.max.x >= point.x && box.min.y <= point.y &&
         box.max.y >= point.y;
}

fuchsia::accessibility::semantics::NodePtr SemanticTree::GetAccessibilityNode(uint32_t node_id) {
  auto node_it = nodes_.find(node_id);
  if (node_it == nodes_.end()) {
    return nullptr;
  }
  auto node_ptr = fuchsia::accessibility::semantics::Node::New();
  node_it->second.Clone(node_ptr.get());
  return node_ptr;
}

bool SemanticTree::IsSameView(const fuchsia::ui::views::ViewRef& view_ref) {
  return GetKoid(view_ref) == GetKoid(view_ref_);
}

bool SemanticTree::IsSameKoid(const zx_koid_t koid) { return koid == GetKoid(view_ref_); }

void SemanticTree::CommitUpdates(CommitUpdatesCallback callback) {
  if (!ApplyCommit()) {
    callback();
    // Call Semantics Manager to close the channel for current tree.
    FX_LOGS(ERROR) << "Closing Semantic Tree Channel for View(KOID):" << GetKoid(view_ref_);
    commit_error_callback_(GetKoid(view_ref_));
  }
}

bool SemanticTree::ApplyCommit() {
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
  fuchsia::accessibility::semantics::NodePtr root_node = GetAccessibilityNode(kRootNode);
  if (!root_node) {
    FX_LOGS(ERROR) << "No root node found after applying commit for view(koid):"
                   << GetKoid(view_ref_);
    nodes_.clear();
    return false;
  }

  // A tree must be acyclic. Delete if cycle found.
  std::unordered_set<uint32_t> visited;
  if (IsCyclic(std::move(root_node), &visited)) {
    FX_LOGS(ERROR) << "Cycle found in semantic tree with View Id:" << GetKoid(view_ref_);
    nodes_.clear();
    return false;
  }

  return true;
}

void SemanticTree::UpdateSemanticNodes(std::vector<fuchsia::accessibility::semantics::Node> nodes) {
  for (auto& node : nodes) {
    if (!node.has_node_id()) {
      // Skip Nodes that are missing node id.
      FX_LOGS(ERROR) << "Update Node is missing node-id which is a required field. Ignoring the "
                        "node as part of the update.";
      continue;
    }
    fuchsia::accessibility::semantics::NodePtr node_ptr = GetAccessibilityNode(node.node_id());

    SemanticTreeTransaction transaction;
    if (node_ptr) {
      // Since node already exist, perform partial update.
      fuchsia::accessibility::semantics::NodePtr updated_node;
      updated_node = UpdateNode(std::move(node), std::move(node_ptr));
      transaction.node_id = updated_node->node_id();
      updated_node->Clone(&transaction.node);
    } else {
      transaction.node_id = node.node_id();
      transaction.node = std::move(node);
    }
    transaction.delete_node = false;
    pending_transactions_.push_back(std::move(transaction));
  }
}

fuchsia::accessibility::semantics::NodePtr SemanticTree::UpdateNode(
    fuchsia::accessibility::semantics::Node input_node,
    fuchsia::accessibility::semantics::NodePtr output_node) {
  // If node id is missing, or different then return.
  if (!input_node.has_node_id() || !output_node->has_node_id() ||
      input_node.node_id() != output_node->node_id()) {
    FX_LOGS(ERROR) << "Update Node is missing a node-id, which is a required field for updates. "
                      "Ignoring this update.";
    return output_node;
  }

  if (input_node.has_role()) {
    output_node->set_role(input_node.role());
  }

  if (input_node.has_states()) {
    output_node->set_states(std::move(*input_node.mutable_states()));
  }

  if (input_node.has_attributes()) {
    output_node->set_attributes(std::move(*input_node.mutable_attributes()));
  }

  if (input_node.has_actions()) {
    output_node->set_actions(input_node.actions());
  }

  if (input_node.has_child_ids()) {
    output_node->set_child_ids(input_node.child_ids());
  }

  if (input_node.has_location()) {
    output_node->set_location(input_node.location());
  }

  if (input_node.has_transform()) {
    output_node->set_transform(input_node.transform());
  }
  return output_node;
}

void SemanticTree::DeleteSemanticNodes(std::vector<uint32_t> node_ids) {
  for (const auto& node_id : node_ids) {
    SemanticTreeTransaction transaction;
    transaction.delete_node = true;
    transaction.node_id = node_id;
    pending_transactions_.push_back(std::move(transaction));
  }
}

// Helper function to traverse semantic tree and create log message.
void SemanticTree::LogSemanticTreeHelper(fuchsia::accessibility::semantics::NodePtr root_node,
                                         int current_level, std::string* tree_log) {
  if (!root_node) {
    return;
  }

  // Add constant spaces proportional to the current tree level, so that
  // child nodes are indented under parent node.
  tree_log->append(kIndentSize * current_level, ' ');

  // Add logs for the current node.
  *tree_log = fxl::Concatenate(
      {*tree_log, "Node_id: ", std::to_string(root_node->node_id()), ", Label:",
       root_node->attributes().has_label() ? root_node->attributes().label() : "_empty", kNewLine});

  // Iterate through all the children of the current node.
  if (!root_node->has_child_ids()) {
    return;
  }
  for (const auto& child : root_node->child_ids()) {
    fuchsia::accessibility::semantics::NodePtr node_ptr = GetAccessibilityNode(child);
    LogSemanticTreeHelper(std::move(node_ptr), current_level + 1, tree_log);
  }
}

std::string SemanticTree::LogSemanticTree() {
  // Get the root node.
  fuchsia::accessibility::semantics::NodePtr node_ptr = GetAccessibilityNode(kRootNode);
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

bool SemanticTree::IsCyclic(fuchsia::accessibility::semantics::NodePtr node,
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
    fuchsia::accessibility::semantics::NodePtr child_ptr = GetAccessibilityNode(child);
    if (!child_ptr) {
      FX_LOGS(ERROR) << "Child Node(id:" << child
                     << ") not found in the semantic tree for View(koid):" << GetKoid(view_ref_);
      continue;
    }
    if (IsCyclic(std::move(child_ptr), visited)) {
      return true;
    }
  }
  return false;
}

void SemanticTree::DeleteSubtree(uint32_t node_id) {
  // Recursively delete the entire subtree at given node_id.
  fuchsia::accessibility::semantics::NodePtr node = GetAccessibilityNode(node_id);
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

void SemanticTree::DeletePointerFromParent(uint32_t node_id) {
  // Assumption: There is only 1 parent per node.
  // In future, we would like to delete trees not rooted at root node.
  // Loop through all the nodes in the tree, since there can be trees not rooted
  // at 0(root-node).
  for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
    if (it->second.has_child_ids()) {
      // Loop through all the children of the node.
      for (auto child_it = it->second.child_ids().begin(); child_it != it->second.child_ids().end();
           ++child_it) {
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

void SemanticTree::InitializeDebugEntry() {
  if (debug_dir_) {
    // Add Semantic Tree log file in Hub-Debug directory.
    debug_dir_->AddEntry(
        std::to_string(GetKoid(view_ref_)),
        std::make_unique<vfs::PseudoFile>(
            kMaxDebugFileSize, [this](std::vector<uint8_t>* output, size_t max_file_size) {
              std::string buffer = LogSemanticTree();
              size_t len = buffer.length();
              if (len > max_file_size) {
                FX_LOGS(WARNING) << "Semantic Tree log file (" << std::to_string(GetKoid(view_ref_))
                                 << ") size is:" << len
                                 << " which is more than max size:" << kMaxDebugFileSize;
                len = kMaxDebugFileSize;
              }
              output->resize(len);
              std::copy(buffer.begin(), buffer.begin() + len, output->begin());
              return ZX_OK;
            }));
  }
}

void SemanticTree::PerformHitTesting(
    ::fuchsia::math::PointF local_point,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  semantic_listener_->HitTest(local_point, std::move(callback));
}

void SemanticTree::EnableSemanticsUpdates(bool enabled) {
  semantics_manager_enabled_ = enabled;
  // If Semantics Manager is disabled, then clear current semantic tree.
  if (!enabled) {
    nodes_.clear();
  }

  // Notify Semantic Provider about Semantics Manager's enable state.
  fuchsia::accessibility::semantics::SemanticListener::OnSemanticsModeChangedCallback callback =
      []() { FX_LOGS(INFO) << "NotifySemanticsEnabled complete."; };
  semantic_listener_->OnSemanticsModeChanged(enabled, std::move(callback));
}

// namespace a11y
}  // namespace a11y
