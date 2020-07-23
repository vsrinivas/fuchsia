// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree.h"

#include <cstdint>

namespace accessibility_test {
MockSemanticTree::MockSemanticTree() {
  previous_node_called_on_ = UINT_MAX;
  next_node_called_on_ = UINT_MAX;
}

const fuchsia::accessibility::semantics::Node* MockSemanticTree::GetPreviousNode(
    uint32_t node_id,
    fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const {
  auto* mock_semantic_tree_ptr = const_cast<MockSemanticTree*>(this);
  mock_semantic_tree_ptr->previous_node_called_on_ = node_id;
  mock_semantic_tree_ptr->get_previous_node_called_ = true;
  return previous_node_result_;
}

void MockSemanticTree::SetPreviousNode(fuchsia::accessibility::semantics::Node* node) {
  previous_node_result_ = node;
}

const fuchsia::accessibility::semantics::Node* MockSemanticTree::GetNextNode(
    uint32_t node_id,
    fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const {
  auto* mock_semantic_tree_ptr = const_cast<MockSemanticTree*>(this);
  mock_semantic_tree_ptr->next_node_called_on_ = node_id;
  mock_semantic_tree_ptr->get_next_node_called_ = true;
  return next_node_result_;
}

void MockSemanticTree::SetNextNode(fuchsia::accessibility::semantics::Node* node) {
  next_node_result_ = node;
}

bool MockSemanticTree::IsGetPreviousNodeCalled() const { return get_previous_node_called_; }

bool MockSemanticTree::IsGetNextNodeCalled() const { return get_next_node_called_; }

uint32_t MockSemanticTree::PreviousNodeCalledOnId() const { return previous_node_called_on_; }

uint32_t MockSemanticTree::NextNodeCalledOnId() const { return next_node_called_on_; }

}  // namespace accessibility_test
