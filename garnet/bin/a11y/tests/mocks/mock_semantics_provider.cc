// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/tests/mocks/mock_semantics_provider.h"

namespace accessibility_test {

MockSemanticsProvider::MockSemanticsProvider(sys::ComponentContext* context,
                                             zx_koid_t view_id)
    : binding_(this), context_(context), view_id_(view_id) {
  context_->svc()->Connect(root_.NewRequest());
  root_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "Cannot connect to semantics root.";
  });
  root_->RegisterSemanticsProvider(view_id, binding_.NewBinding());
}

void MockSemanticsProvider::UpdateSemanticsNodes(
    fidl::VectorPtr<fuchsia::accessibility::Node> update_nodes) {
  root_->UpdateSemanticNodes(view_id_, std::move(update_nodes));
}

void MockSemanticsProvider::DeleteSemanticsNodes(
    fidl::VectorPtr<int32_t> delete_nodes) {
  root_->DeleteSemanticNodes(view_id_, std::move(delete_nodes));
}

void MockSemanticsProvider::Commit() { root_->Commit(view_id_); }

}  // namespace accessibility_test
