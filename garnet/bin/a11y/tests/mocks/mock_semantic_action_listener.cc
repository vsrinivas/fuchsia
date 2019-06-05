// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/tests/mocks/mock_semantic_action_listener.h"

#include <lib/syslog/cpp/logger.h>

namespace accessibility_test {

MockSemanticActionListener::MockSemanticActionListener(
    sys::ComponentContext* context, fuchsia::ui::views::ViewRef view_ref)
    : context_(context), view_ref_(std::move(view_ref)) {
  context_->svc()->Connect(manager_.NewRequest());
  manager_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Cannot connect to SemanticsManager with status:"
                   << status;
  });
  fidl::InterfaceHandle<
      fuchsia::accessibility::semantics::SemanticActionListener>
      listener_handle;
  bindings_.AddBinding(this, listener_handle.NewRequest());
  manager_->RegisterView(std::move(view_ref_), std::move(listener_handle),
                         tree_ptr_.NewRequest());
}

void MockSemanticActionListener::UpdateSemanticNodes(
    std::vector<fuchsia::accessibility::semantics::Node> nodes) {
  tree_ptr_->UpdateSemanticNodes(std::move(nodes));
}

void MockSemanticActionListener::DeleteSemanticNodes(
    std::vector<uint32_t> node_ids) {
  tree_ptr_->DeleteSemanticNodes(std::move(node_ids));
}

void MockSemanticActionListener::Commit() { tree_ptr_->Commit(); }
}  // namespace accessibility_test
