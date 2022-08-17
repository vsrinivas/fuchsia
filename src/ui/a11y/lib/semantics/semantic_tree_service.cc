// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"

#include <lib/async/default.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <algorithm>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/strings/concatenate.h"
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
    std::unique_ptr<::a11y::SemanticTree> tree, zx_koid_t(koid),
    fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
    CloseChannelCallback error_callback)
    : tree_(std::move(tree)),
      close_channel_callback_(std::move(error_callback)),
      koid_(koid),
      semantic_listener_(std::move(semantic_listener)),
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
}

SemanticTreeService::~SemanticTreeService() { semantic_tree_factory_->InvalidateWeakPtrs(); }

void SemanticTreeService::PerformAccessibilityAction(
    uint32_t node_id, fuchsia::accessibility::semantics::Action action,
    fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
        callback) {
  semantic_listener_->OnAccessibilityActionRequested(node_id, action, std::move(callback));
}

zx_koid_t SemanticTreeService::view_ref_koid() const { return koid_; }

void SemanticTreeService::CommitUpdates(CommitUpdatesCallback callback) {
  if (!semantic_updates_enabled_) {
    FX_LOGS(INFO) << "Ignoring Commit while semantics are disabled.";
    return;
  }
  if (tree_->Update(std::move(updates_))) {
    callback();
    updates_.clear();
  } else {
    // Work around https://fxbug.dev/70758
    std::string tree_str = tree_->ToString();
    constexpr size_t kMaxLength = 30000;
    if (tree_str.size() > kMaxLength) {
      tree_str.resize(kMaxLength);
    }
    FX_LOGS(ERROR) << "Closing Semantic Tree Channel for View(KOID):" << koid_
                   << " because client sent an invalid tree update. Tree before update: "
                   << tree_str;
    callback();
    close_channel_callback_(ZX_ERR_INVALID_ARGS);
  }
}

void SemanticTreeService::UpdateSemanticNodes(std::vector<Node> nodes) {
  if (!semantic_updates_enabled_) {
    FX_LOGS(INFO) << "Ignoring Update while semantics are disabled.";
    return;
  }
  for (auto& node : nodes) {
    updates_.emplace_back(std::move(node));
  }
}

void SemanticTreeService::DeleteSemanticNodes(std::vector<uint32_t> node_ids) {
  if (!semantic_updates_enabled_) {
    FX_LOGS(INFO) << "Ignoring Delete while semantics are disabled.";
    return;
  }

  for (const auto& node_id : node_ids) {
    updates_.emplace_back(node_id);
  }
}

void SemanticTreeService::PerformHitTesting(
    ::fuchsia::math::PointF local_point,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  semantic_listener_->HitTest(local_point, std::move(callback));
}

void SemanticTreeService::EnableSemanticsUpdates(bool enabled) {
  semantic_updates_enabled_ = enabled;
  // If Semantics Manager is disabled, then clear current semantic tree.
  if (!enabled) {
    tree_->Clear();
  }

  // Notify Semantic Provider about Semantics Manager's enable state.
  fuchsia::accessibility::semantics::SemanticListener::OnSemanticsModeChangedCallback callback =
      []() { FX_LOGS(INFO) << "NotifySemanticsEnabled complete."; };
  semantic_listener_->OnSemanticsModeChanged(enabled, std::move(callback));
}

const fxl::WeakPtr<::a11y::SemanticTree> SemanticTreeService::Get() const {
  FX_DCHECK(semantic_tree_factory_.get());
  return semantic_tree_factory_->GetWeakPtr();
}

void SemanticTreeService::SendSemanticEvent(
    fuchsia::accessibility::semantics::SemanticEvent semantic_event,
    SendSemanticEventCallback callback) {
  SemanticsEventInfo event;
  event.semantic_event = std::move(semantic_event);
  tree_->OnSemanticsEvent(std::move(event));
  callback();
}

std::unique_ptr<SemanticTreeService> SemanticTreeServiceFactory::NewService(
    zx_koid_t koid, fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
    SemanticTreeService::CloseChannelCallback close_channel_callback,
    SemanticTree::SemanticsEventCallback semantics_event_callback) {
  auto inspect_name = "semantic_tree_" + std::to_string(koid);
  auto tree_ptr = std::make_unique<SemanticTree>(inspect_node_.CreateChild(inspect_name));
  tree_ptr->set_semantics_event_callback(std::move(semantics_event_callback));
  auto semantic_tree = std::make_unique<SemanticTreeService>(
      std::move(tree_ptr), koid, std::move(semantic_listener), std::move(close_channel_callback));
  return semantic_tree;
}

}  // namespace a11y
