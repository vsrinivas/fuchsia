// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"

namespace accessibility_test {

fuchsia::ui::views::ViewRef MockSemanticProvider::CreateOrphanViewRef() {
  fuchsia::ui::views::ViewRef view_ref;

  FX_CHECK(zx::eventpair::create(0u, &view_ref.reference, &eventpair_peer_) == ZX_OK);
  return view_ref;
}

namespace {
fuchsia::ui::views::ViewRef Clone(const fuchsia::ui::views::ViewRef& view_ref) {
  fuchsia::ui::views::ViewRef clone;
  FX_CHECK(fidl::Clone(view_ref, &clone) == ZX_OK);
  return clone;
}

}  // namespace

MockSemanticProvider::MockSemanticProvider(
    fuchsia::accessibility::semantics::SemanticsManager* manager)
    : view_ref_(CreateOrphanViewRef()) {
  manager->RegisterViewForSemantics(Clone(view_ref_),
                                    semantic_listener_bindings_.AddBinding(&semantic_listener_),
                                    tree_ptr_.NewRequest());
  commit_failed_ = false;
}

void MockSemanticProvider::UpdateSemanticNodes(
    std::vector<fuchsia::accessibility::semantics::Node> nodes) {
  tree_ptr_->UpdateSemanticNodes(std::move(nodes));
}

void MockSemanticProvider::DeleteSemanticNodes(std::vector<uint32_t> node_ids) {
  tree_ptr_->DeleteSemanticNodes(std::move(node_ids));
}

void MockSemanticProvider::CommitUpdates() {
  tree_ptr_->CommitUpdates([this]() { commit_failed_ = true; });
}

void MockSemanticProvider::SetHitTestResult(uint32_t hit_test_result) {
  semantic_listener_.SetHitTestResult(hit_test_result);
}

bool MockSemanticProvider::GetSemanticsEnabled() {
  return semantic_listener_.GetSemanticsEnabled();
}

void MockSemanticProvider::SendEventPairSignal() {
  // Reset event pair peer. This should call close on the event pair that will send peer closed
  // signal.
  eventpair_peer_.reset();
}

bool MockSemanticProvider::IsChannelClosed() { return !tree_ptr_.channel().is_valid(); }

}  // namespace accessibility_test
