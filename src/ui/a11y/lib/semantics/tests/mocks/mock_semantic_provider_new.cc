// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider_new.h"

#include <lib/syslog/cpp/logger.h>

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"

namespace accessibility_test {
namespace {

fuchsia::ui::views::ViewRef CreateOrphanViewRef() {
  fuchsia::ui::views::ViewRef view_ref;
  zx::eventpair unused;
  FX_CHECK(zx::eventpair::create(0u, &view_ref.reference, &unused) == ZX_OK);
  return view_ref;
}

fuchsia::ui::views::ViewRef Clone(const fuchsia::ui::views::ViewRef& view_ref) {
  fuchsia::ui::views::ViewRef clone;
  FX_CHECK(fidl::Clone(view_ref, &clone) == ZX_OK);
  return clone;
}

}  // namespace

MockSemanticProviderNew::MockSemanticProviderNew(
    fuchsia::accessibility::semantics::SemanticsManager* manager)
    : view_ref_(CreateOrphanViewRef()) {
  manager->RegisterViewForSemantics(Clone(view_ref_),
                                    semantic_listener_bindings_.AddBinding(&semantic_listener_),
                                    tree_ptr_.NewRequest());

  commit_failed_ = false;
}

void MockSemanticProviderNew::UpdateSemanticNodes(
    std::vector<fuchsia::accessibility::semantics::Node> nodes) {
  tree_ptr_->UpdateSemanticNodes(std::move(nodes));
}

void MockSemanticProviderNew::DeleteSemanticNodes(std::vector<uint32_t> node_ids) {
  tree_ptr_->DeleteSemanticNodes(std::move(node_ids));
}

void MockSemanticProviderNew::Commit() { tree_ptr_->Commit(); }

void MockSemanticProviderNew::CommitUpdates() {
  tree_ptr_->CommitUpdates([this]() { commit_failed_ = true; });
}

void MockSemanticProviderNew::SetHitTestResult(uint32_t hit_test_result) {
  semantic_listener_.SetHitTestResult(hit_test_result);
}

bool MockSemanticProviderNew::GetSemanticsEnabled() {
  return semantic_listener_.GetSemanticsEnabled();
}

}  // namespace accessibility_test
