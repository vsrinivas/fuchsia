// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"

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

using fuchsia::accessibility::semantics::Node;

// Max file size of semantic tree log file is 1MB.
constexpr size_t kMaxDebugFileSize = 1024 * 1024;
const std::string kNewLine = "\n";
constexpr std::string::size_type kIndentSize = 4;

}  // namespace

SemanticTreeService::SemanticTreeService(
    std::unique_ptr<::a11y::SemanticTree> tree, fuchsia::ui::views::ViewRef view_ref,
    fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
    vfs::PseudoDir* debug_dir, CloseChannelCallback error_callback)
    : tree_(std::move(tree)),
      close_channel_callback_(std::move(error_callback)),
      view_ref_(std::move(view_ref)),
      wait_(this, view_ref_.reference.get(), ZX_EVENTPAIR_PEER_CLOSED),
      semantic_listener_(std::move(semantic_listener)),
      debug_dir_(debug_dir),
      semantic_tree_factory_(
          std::make_unique<fxl::WeakPtrFactory<::a11y::SemanticTree>>(tree_.get())) {
  tree_->set_action_handler([this](uint32_t node_id,
                                   fuchsia::accessibility::semantics::Action action,
                                   fuchsia::accessibility::semantics::SemanticListener::
                                       OnAccessibilityActionRequestedCallback callback) {
    this->PerformAccessibilityAction(node_id, action, std::move(callback));
  });
  tree_->set_hit_testing_handler(
      [this](fuchsia::math::PointF local_point,
             fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
        this->PerformHitTesting(local_point, std::move(callback));
      });
  wait_.Begin(async_get_default_dispatcher());
  InitializeDebugEntry();
}

SemanticTreeService::~SemanticTreeService() { semantic_tree_factory_->InvalidateWeakPtrs(); }

void SemanticTreeService::PerformAccessibilityAction(
    uint32_t node_id, fuchsia::accessibility::semantics::Action action,
    fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
        callback) {
  semantic_listener_->OnAccessibilityActionRequested(node_id, action, std::move(callback));
}

zx_koid_t SemanticTreeService::view_ref_koid() const { return GetKoid(view_ref_); }

void SemanticTreeService::CommitUpdates(CommitUpdatesCallback callback) {
  if (tree_->Update(std::move(updates_))) {
    callback();
    updates_.clear();
  } else {
    FX_LOGS(ERROR) << "Closing Semantic Tree Channel for View(KOID):" << GetKoid(view_ref_)
                   << " because client sent an invalid tree update";
    callback();
    close_channel_callback_(GetKoid(view_ref_));
  }
}

void SemanticTreeService::UpdateSemanticNodes(std::vector<Node> nodes) {
  for (auto& node : nodes) {
    updates_.emplace_back(std::move(node));
  }
}

void SemanticTreeService::DeleteSemanticNodes(std::vector<uint32_t> node_ids) {
  for (const auto& node_id : node_ids) {
    updates_.emplace_back(node_id);
  }
}

void SemanticTreeService::LogSemanticTreeHelper(const Node* node, int current_level,
                                                std::string* tree_log) {
  if (!node) {
    return;
  }

  // Add constant spaces proportional to the current tree level, so that
  // child nodes are indented under parent node.
  tree_log->append(kIndentSize * current_level, ' ');

  // Add logs for the current node.
  *tree_log = fxl::Concatenate({*tree_log, "Node_id: ", std::to_string(node->node_id()), ", Label:",
                                node->has_attributes() && node->attributes().has_label()
                                    ? node->attributes().label()
                                    : "_empty",
                                kNewLine});

  // Iterate through all the children of the current node.
  if (!node->has_child_ids()) {
    return;
  }
  for (const auto& child_id : node->child_ids()) {
    const auto* child = tree_->GetNode(child_id);
    FX_DCHECK(child);
    LogSemanticTreeHelper(child, current_level + 1, tree_log);
  }
}

std::string SemanticTreeService::LogSemanticTree() {
  const auto* root = tree_->GetNode(::a11y::SemanticTree::kRootNodeId);
  std::string tree_log;
  if (!root) {
    tree_log = "Root Node not found.";
    FX_LOGS(ERROR) << tree_log;
    return tree_log;
  }

  LogSemanticTreeHelper(root, ::a11y::SemanticTree::kRootNodeId, &tree_log);
  FX_VLOGS(1) << "Semantic Tree:" << std::endl << tree_log;
  return tree_log;
}

void SemanticTreeService::InitializeDebugEntry() {
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

void SemanticTreeService::PerformHitTesting(
    ::fuchsia::math::PointF local_point,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  semantic_listener_->HitTest(local_point, std::move(callback));
}

void SemanticTreeService::EnableSemanticsUpdates(bool enabled) {
  semantics_manager_enabled_ = enabled;
  // If Semantics Manager is disabled, then clear current semantic tree.
  if (!enabled) {
    tree_->Clear();
  }

  // Notify Semantic Provider about Semantics Manager's enable state.
  fuchsia::accessibility::semantics::SemanticListener::OnSemanticsModeChangedCallback callback =
      []() { FX_LOGS(INFO) << "NotifySemanticsEnabled complete."; };
  semantic_listener_->OnSemanticsModeChanged(enabled, std::move(callback));
}

void SemanticTreeService::SignalHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                        zx_status_t status, const zx_packet_signal* signal) {
  close_channel_callback_(GetKoid(view_ref_));
}

const fxl::WeakPtr<::a11y::SemanticTree> SemanticTreeService::Get() const {
  FX_DCHECK(semantic_tree_factory_.get());
  return semantic_tree_factory_->GetWeakPtr();
}

}  // namespace a11y
