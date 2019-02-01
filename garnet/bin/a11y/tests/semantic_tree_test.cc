// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "garnet/bin/a11y/a11y_manager/semantic_tree.h"
#include "garnet/bin/a11y/tests/mocks/mock_semantics_provider.h"
#include "gtest/gtest.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/component/cpp/testing/test_with_context.h"

namespace a11y_manager_test {

// Unit tests for garnet/bin/a11y/a11y_manager/semantic_tree.h
class SemanticTreeTest : public component::testing::TestWithContext {
 public:
  void SetUp() override {
    TestWithContext::SetUp();
    controller().AddService<fuchsia::accessibility::SemanticsRoot>(
        [this](fidl::InterfaceRequest<fuchsia::accessibility::SemanticsRoot>
                   request) { tree_.AddBinding(std::move(request)); });
    context_ = TakeContext();
    RunLoopUntilIdle();
  }

  a11y_manager::SemanticTree tree_;
  std::unique_ptr<component::StartupContext> context_;
};

// Create a test node with only a node id and a label.
fuchsia::accessibility::Node CreateTestNode(int32_t node_id,
                                            std::string label) {
  fuchsia::accessibility::Node node = fuchsia::accessibility::Node();
  node.node_id = node_id;
  node.children_hit_test_order = fidl::VectorPtr<int32_t>::New(0);
  node.children_traversal_order = fidl::VectorPtr<int32_t>::New(0);
  node.data = fuchsia::accessibility::Data();
  node.data.role = fuchsia::accessibility::Role::NONE;
  node.data.label = std::move(label);
  fuchsia::ui::gfx::BoundingBox box;
  node.data.location = std::move(box);
  fuchsia::ui::gfx::mat4 transform;
  node.data.transform = std::move(transform);
  return node;
}

// Basic test to check that a node can be updated, committed and then deleted.
TEST_F(SemanticTreeTest, NodeUpdateDelete) {
  zx_koid_t view_id = 0;
  accessibility_test::MockSemanticsProvider provider(context_.get(), view_id);
  // We make sure the provider has finished connecting to the root.
  RunLoopUntilIdle();

  // Creating test node to update.
  fidl::VectorPtr<fuchsia::accessibility::Node> update_nodes;
  fuchsia::accessibility::Node node = CreateTestNode(0, "Label A");
  fuchsia::accessibility::Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));

  // Updating: No node should be found because we have not committed.
  provider.UpdateSemanticsNodes(std::move(update_nodes));
  RunLoopUntilIdle();
  EXPECT_EQ(nullptr, tree_.GetAccessibilityNode(view_id, 0));

  // Committing: The node should be found.
  provider.Commit();
  RunLoopUntilIdle();
  fuchsia::accessibility::NodePtr returned_node =
      tree_.GetAccessibilityNode(view_id, 0);
  EXPECT_NE(returned_node, nullptr);
  EXPECT_EQ(node.node_id, returned_node->node_id);
  EXPECT_STREQ(node.data.label.data(), returned_node->data.label.data());

  // Deleting: The node should be deleted and not be found.
  fidl::VectorPtr<int32_t> delete_nodes;
  delete_nodes.push_back(node.node_id);
  provider.DeleteSemanticsNodes(std::move(delete_nodes));
  provider.Commit();
  RunLoopUntilIdle();

  // No node should be found because we have deleted the node.
  EXPECT_EQ(nullptr, tree_.GetAccessibilityNode(view_id, 0));
}

}  // namespace a11y_manager_test
