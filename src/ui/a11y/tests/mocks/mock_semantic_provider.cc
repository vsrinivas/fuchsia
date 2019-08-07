// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/tests/mocks/mock_semantic_provider.h"

#include <lib/syslog/cpp/logger.h>

namespace accessibility_test {

MockSemanticProvider::MockSemanticProvider(sys::ComponentContext* context,
                                           fuchsia::ui::views::ViewRef view_ref)
    : view_ref_(std::move(view_ref)) {
  context->svc()->Connect(manager_.NewRequest());
  manager_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Cannot connect to SemanticsManager with status:" << status;
  });
  fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticActionListener> listener_handle;
  action_listener_.Bind(&listener_handle);
  manager_->RegisterView(std::move(view_ref_), std::move(listener_handle), tree_ptr_.NewRequest());
}

void MockSemanticProvider::UpdateSemanticNodes(
    std::vector<fuchsia::accessibility::semantics::Node> nodes) {
  tree_ptr_->UpdateSemanticNodes(std::move(nodes));
}

void MockSemanticProvider::DeleteSemanticNodes(std::vector<uint32_t> node_ids) {
  tree_ptr_->DeleteSemanticNodes(std::move(node_ids));
}

void MockSemanticProvider::Commit() { tree_ptr_->Commit(); }

void MockSemanticProvider::SetHitTestResult(uint32_t hit_test_result) {
  action_listener_.SetHitTestResult(hit_test_result);
}

}  // namespace accessibility_test
