// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/testing/fake_a11y_manager.h"

namespace a11y_testing {

FakeSemanticTree::FakeSemanticTree(
    fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener)
    : semantic_listener_(std::move(semantic_listener)), semantic_tree_binding_(this) {}

void FakeSemanticTree::CommitUpdates(CommitUpdatesCallback callback) { callback(); }

void FakeSemanticTree::Bind(
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request) {
  semantic_tree_binding_.Bind(std::move(semantic_tree_request));
}

void FakeSemanticTree::UpdateSemanticNodes(
    std::vector<fuchsia::accessibility::semantics::Node> nodes) {}

void FakeSemanticTree::DeleteSemanticNodes(std::vector<uint32_t> node_ids) {}

void FakeSemanticTree::SetSemanticsEnabled(bool enabled) {
  semantic_listener_->OnSemanticsModeChanged(enabled, []() {});
}

fidl::InterfaceRequestHandler<fuchsia::accessibility::semantics::SemanticsManager>
FakeA11yManager::GetHandler() {
  return semantics_manager_bindings_.GetHandler(this);
}

void FakeA11yManager::RegisterViewForSemantics(
    fuchsia::ui::views::ViewRef view_ref,
    fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener> handle,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request) {
  fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener;
  semantic_listener.Bind(std::move(handle));
  semantic_trees_.emplace_back(std::make_unique<FakeSemanticTree>(std::move(semantic_listener)));
  semantic_trees_.back()->Bind(std::move(semantic_tree_request));
  semantic_trees_.back()->SetSemanticsEnabled(false);
}

}  // namespace a11y_testing
