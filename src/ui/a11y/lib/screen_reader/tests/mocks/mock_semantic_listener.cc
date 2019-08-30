// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_semantic_listener.h"

#include <lib/syslog/cpp/logger.h>
namespace accessibility_test {

MockSemanticListener::MockSemanticListener(SemanticsManager* manager,
                                           fuchsia::ui::views::ViewRef view_ref) {
  manager->RegisterView(std::move(view_ref), bindings_.AddBinding(this), tree_ptr_.NewRequest());
}

void MockSemanticListener::UpdateSemanticNodes(std::vector<Node> nodes) {
  tree_ptr_->UpdateSemanticNodes(std::move(nodes));
}

void MockSemanticListener::DeleteSemanticNodes(std::vector<uint32_t> node_ids) {
  tree_ptr_->DeleteSemanticNodes(std::move(node_ids));
}

void MockSemanticListener::Commit() { tree_ptr_->Commit(); }

void MockSemanticListener::HitTest(::fuchsia::math::PointF local_point, HitTestCallback callback) {
  Hit hit;
  hit_test_result_.Clone(&hit);
  callback(std::move(hit));
}

}  // namespace accessibility_test
