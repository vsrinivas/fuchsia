// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/semantic_tree.h"

#include <lib/async/default.h>
#include <zircon/types.h>

#include <algorithm>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/syslog/cpp/logger.h"
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
                           vfs::PseudoDir* debug_dir, CloseChannelCallback error_callback)
    : close_channel_callback_(std::move(error_callback)),
      view_ref_(std::move(view_ref)),
      wait_(this, view_ref_.reference.get(), ZX_EVENTPAIR_PEER_CLOSED),
      semantic_listener_(std::move(semantic_listener)),
      debug_dir_(debug_dir) {
  wait_.Begin(async_get_default_dispatcher());
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
                                    const fuchsia::math::PointF& point) {
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
    callback();  // Notifies that the commit was processed before closing the channel.
    // Call Semantics Manager to close the channel for current tree.
    FX_LOGS(ERROR) << "Closing Semantic Tree Channel for View(KOID):" << GetKoid(view_ref_);
    close_channel_callback_(GetKoid(view_ref_));
  } else {
    callback();
  }
}

bool SemanticTree::ApplyCommit() {
  // TODO(MI4-2038): Commit should ensure that there is only 1 tree rooted at
  // node_id 0.

  // Apply transactions in the order in which they have arrived.
  for (auto& transaction : pending_transactions_) {
    if (transaction.delete_node) {
      // Delete node.
      nodes_.erase(transaction.node_id);
    } else {
      // Update Node.
      nodes_[transaction.node_id] = std::move(transaction.node);
    }
  }

  // Remove deleted node ids from their respective parents' child_ids.
  for (auto& node_id_and_node_pair : nodes_) {
    auto& node = node_id_and_node_pair.second;

    if (!node.has_child_ids()) {
      continue;
    }

    auto child_ids = node.mutable_child_ids();
    auto retained_child_ids_end =
        std::remove_if(child_ids->begin(), child_ids->end(),
                       [&](uint32_t child_id) { return nodes_.count(child_id) == 0; });
    child_ids->erase(retained_child_ids_end, child_ids->end());
  }

  // Clear list of pending transactions.
  pending_transactions_.clear();

  // Get root node of the tree after all the updates/commits are applied.
  // Only empty semantic trees are allowed to not have a root node.
  fuchsia::accessibility::semantics::NodePtr root_node = GetAccessibilityNode(kRootNode);
  if (!root_node) {
    FX_LOGS(INFO) << "No root node found after applying commit for view(koid):"
                  << GetKoid(view_ref_);
    if (!nodes_.empty()) {
      nodes_.clear();
      return false;
    }
    return true;
  }

  // Semantic tree must be acyclic. Delete if cycle found.
  // This also ensures that every node has exactly one parent.
  std::unordered_set<uint32_t> visited;
  if (!IsTreeWellFormed(std::move(root_node), &visited)) {
    FX_LOGS(ERROR) << "Semantic tree with View Id:" << GetKoid(view_ref_) << " is not well formed.";
    nodes_.clear();
    return false;
  }

  // Using visited node information, check if every node is reachable from the root node and there
  // are no dangling subtrees.
  if (!CheckIfAllNodesReachable(visited)) {
    FX_LOGS(ERROR) << "Multiple subtrees found in semantic tree with View Id:"
                   << GetKoid(view_ref_);
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
  *tree_log =
      fxl::Concatenate({*tree_log, "Node_id: ", std::to_string(root_node->node_id()), ", Label:",
                        root_node->has_attributes() && root_node->attributes().has_label()
                            ? root_node->attributes().label()
                            : "_empty",
                        kNewLine});

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

void SemanticTree::InitializeNodesForTest(
    std::vector<fuchsia::accessibility::semantics::Node> nodes) {
  for (auto& node : nodes) {
    nodes_[node.node_id()] = std::move(node);
  }
}

std::vector<uint32_t> SemanticTree::GetPendingDeletions() {
  std::vector<uint32_t> pending_deletions;
  for (auto& pending_transaction : pending_transactions_) {
    if (pending_transaction.delete_node) {
      pending_deletions.push_back(pending_transaction.node_id);
    }
  }

  return pending_deletions;
}

std::vector<fuchsia::accessibility::semantics::Node> SemanticTree::GetPendingUpdates() {
  std::vector<fuchsia::accessibility::semantics::Node> pending_updates;
  for (auto& pending_transaction : pending_transactions_) {
    if (pending_transaction.delete_node) {
      continue;
    }

    fuchsia::accessibility::semantics::Node pending_update_node;
    pending_transaction.node.Clone(&pending_update_node);
    pending_updates.push_back(std::move(pending_update_node));
  }

  return pending_updates;
}

void SemanticTree::AddPendingTransaction(const uint32_t node_id, bool delete_node,
                                         fuchsia::accessibility::semantics::Node node) {
  SemanticTreeTransaction transaction;
  transaction.node_id = node_id;
  transaction.delete_node = delete_node;
  node.Clone(&transaction.node);

  pending_transactions_.push_back(std::move(transaction));

  FX_LOGS(INFO) << "Pending transaction node id: " << pending_transactions_.back().node_id;
}

bool SemanticTree::IsTreeWellFormed(fuchsia::accessibility::semantics::NodePtr node,
                                    std::unordered_set<uint32_t>* visited) {
  FXL_CHECK(node);
  FXL_CHECK(visited);

  if (visited->count(node->node_id()) > 0) {
    // Cycle Found.
    return false;
  }
  visited->insert(node->node_id());

  if (!node->has_child_ids()) {
    return true;
  }
  for (const auto& child : node->child_ids()) {
    fuchsia::accessibility::semantics::NodePtr child_ptr = GetAccessibilityNode(child);

    if (!NodeExists(child_ptr, child)) {
      return false;
    }
    if (!IsTreeWellFormed(std::move(child_ptr), visited)) {
      return false;
    }
  }
  return true;
}

bool SemanticTree::NodeExists(const fuchsia::accessibility::semantics::NodePtr& node_ptr,
                              uint32_t node_id) {
  if (!node_ptr) {
    FX_LOGS(ERROR) << "Child Node(id:" << node_id
                   << ") not found in the semantic tree for View(koid):" << GetKoid(view_ref_);
    return false;
  }
  return true;
}

bool SemanticTree::CheckIfAllNodesReachable(const std::unordered_set<uint32_t>& visited) {
  if (visited.size() != size(nodes_)) {
    FX_LOGS(ERROR) << "All nodes in the semantic are not reachable. Semantic Tree size:"
                   << size(nodes_) << ", Visited Nodes List size :" << visited.size();
    return false;
  }
  return true;
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

void SemanticTree::SignalHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                 zx_status_t status, const zx_packet_signal* signal) {
  close_channel_callback_(GetKoid(view_ref_));
}

// namespace a11y
}  // namespace a11y
