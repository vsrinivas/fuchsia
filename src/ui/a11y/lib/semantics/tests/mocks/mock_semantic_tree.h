// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_TREE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_TREE_H_

#include "src/ui/a11y/lib/semantics/semantic_tree.h"

namespace accessibility_test {
class MockSemanticTree : public a11y::SemanticTree {
 public:
  MockSemanticTree();
  ~MockSemanticTree() override = default;

  // Returns previous_node.
  const fuchsia::accessibility::semantics::Node* GetPreviousNode(
      uint32_t node_id,
      fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const override;

  // Function for setting results for GetPreviousNode().
  void SetPreviousNode(fuchsia::accessibility::semantics::Node* node);

  // Returns next_node.
  const fuchsia::accessibility::semantics::Node* GetNextNode(
      uint32_t node_id,
      fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const override;

  // Function for setting results for GetNextNode().
  void SetNextNode(fuchsia::accessibility::semantics::Node* node);

  // Returns true if GetPreviousNodeCalled().
  bool IsGetPreviousNodeCalled() const;

  // Returns true if GetNextNodeCalled().
  bool IsGetNextNodeCalled() const;

  // Returns id of node on which GetPreviousNode() was called.
  uint32_t PreviousNodeCalledOnId() const;

  // Returns id of node on which GetNextNode() was called.
  uint32_t NextNodeCalledOnId() const;

 private:
  // Tracks if GetNextNode() is called.
  bool get_next_node_called_ = false;

  // Tracks if GetPreviousNode() is called.
  bool get_previous_node_called_ = false;

  // Stores result for GetPreviousNode().
  fuchsia::accessibility::semantics::Node* previous_node_result_ = nullptr;

  // Stores result for GetNextNode().
  fuchsia::accessibility::semantics::Node* next_node_result_ = nullptr;

  // Stores the node_id on which GetPreviousNode() is called.
  uint32_t previous_node_called_on_;

  // Stores the node_id on which GetNextNode() is called.
  uint32_t next_node_called_on_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_TREE_H_
