// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/logger.h>

#include "garnet/bin/a11y/a11y_manager/semantic_tree.h"
#include "garnet/bin/a11y/tests/mocks/mock_semantics_provider.h"
#include "gtest/gtest.h"

namespace a11y_manager_test {
const std::string kSemanticTree1 = "    Node_id: 0, Label:Node-0\n";
const std::string kSemanticTree7 =
    "    Node_id: 0, Label:Node-0\n"
    "        Node_id: 1, Label:Node-1\n"
    "            Node_id: 3, Label:Node-3\n"
    "            Node_id: 4, Label:Node-4\n"
    "        Node_id: 2, Label:Node-2\n"
    "            Node_id: 5, Label:Node-5\n"
    "            Node_id: 6, Label:Node-6\n";
const std::string kSemanticTree8 =
    "    Node_id: 0, Label:Node-0\n"
    "        Node_id: 1, Label:Node-1\n"
    "            Node_id: 3, Label:Node-3\n"
    "                Node_id: 7, Label:Node-7\n"
    "            Node_id: 4, Label:Node-4\n"
    "        Node_id: 2, Label:Node-2\n"
    "            Node_id: 5, Label:Node-5\n"
    "            Node_id: 6, Label:Node-6\n";
// Unit tests for garnet/bin/a11y/a11y_manager/semantic_tree.h
class SemanticTreeTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    syslog::InitLogger();
    context_provider_.service_directory_provider()
        ->AddService<fuchsia::accessibility::SemanticsRoot>(
            [this](fidl::InterfaceRequest<fuchsia::accessibility::SemanticsRoot>
                       request) { tree_.AddBinding(std::move(request)); });
    context_ = context_provider_.TakeContext();
    RunLoopUntilIdle();
  }
  void CreateSemanticTree(
      int number_of_nodes_per_view,
      fidl::VectorPtr<fuchsia::accessibility::Node> *nodes_list);
  void InitializeSemanticProvider(
      int number_of_nodes_per_view,
      accessibility_test::MockSemanticsProvider *provider);
  a11y_manager::SemanticTree tree_;
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<sys::ComponentContext> context_;
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

void SemanticTreeTest::CreateSemanticTree(
    int number_of_nodes_per_view,
    fidl::VectorPtr<fuchsia::accessibility::Node> *nodes_list) {
  // Create semantic tree like a Complete Binary Tree for testing purpose.
  for (int node_id = 0; node_id < number_of_nodes_per_view; node_id++) {
    // Create individual Node.
    fuchsia::accessibility::Node node;
    node.node_id = node_id;
    fuchsia::accessibility::Data data;
    data.label = std::string("Node-") + std::to_string(node_id);
    node.data = data;
    // Update Child Traversal Information.
    if ((2 * node_id + 1) < number_of_nodes_per_view) {
      node.children_traversal_order.push_back(2 * node_id + 1);
    }
    if ((2 * node_id + 2) < number_of_nodes_per_view) {
      node.children_traversal_order.push_back(2 * node_id + 2);
    }
    // Add to the list of nodes.
    nodes_list->push_back(std::move(node));
  }
}

void SemanticTreeTest::InitializeSemanticProvider(
    int number_of_nodes_per_view,
    accessibility_test::MockSemanticsProvider *provider) {
  // Create Node List for the current provider.
  fidl::VectorPtr<fuchsia::accessibility::Node> nodes_list;
  CreateSemanticTree(number_of_nodes_per_view, &nodes_list);

  // Add nodes list to the current semantic providers list.
  provider->UpdateSemanticsNodes(std::move(nodes_list));
  RunLoopUntilIdle();

  // Commit the nodes.
  provider->Commit();
  RunLoopUntilIdle();
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

// Test for LogSemanticTree() to make sure correct logs are generated,
// when number of nodes in the tree are odd.
TEST_F(SemanticTreeTest, LogSemanticTree_OddNumberOfNodes) {
  zx_koid_t view_id = 0;
  accessibility_test::MockSemanticsProvider provider(context_.get(), view_id);
  // Make sure the provider has finished connecting to the root.
  RunLoopUntilIdle();

  int number_of_nodes = 7;
  InitializeSemanticProvider(number_of_nodes, &provider);

  std::string result = tree_.LogSemanticTree(view_id);
  EXPECT_EQ(kSemanticTree7, result);
}
// Test for LogSemanticTree() to make sure correct logs are generated,
// when number of nodes in the tree are even.
TEST_F(SemanticTreeTest, LogSemanticTree_EvenNumberOfNodes) {
  zx_koid_t view_id = 0;
  accessibility_test::MockSemanticsProvider provider(context_.get(), view_id);
  // Make sure the provider has finished connecting to the root.
  RunLoopUntilIdle();

  int number_of_nodes = 8;
  InitializeSemanticProvider(number_of_nodes, &provider);

  std::string result = tree_.LogSemanticTree(view_id);
  EXPECT_EQ(kSemanticTree8, result);
}

// Test for LogSemanticTree() to make sure correct logs are generated,
// when there is just a single node in the tree for a particular view.
TEST_F(SemanticTreeTest, LogSemanticTree_SingleNode) {
  zx_koid_t view_id = 0;
  accessibility_test::MockSemanticsProvider provider(context_.get(), view_id);
  // Make sure the provider has finished connecting to the root.
  RunLoopUntilIdle();

  int number_of_nodes = 1;
  InitializeSemanticProvider(number_of_nodes, &provider);

  std::string result = tree_.LogSemanticTree(view_id);
  EXPECT_EQ(kSemanticTree1, result);
}

// Test for LogSemanticTree() to make sure correct logs are generated
// when view id does not match.
TEST_F(SemanticTreeTest, LogSemanticTree_ViewNotFound) {
  zx_koid_t view_id = 0;
  zx_koid_t view_id_to_search = 1;
  accessibility_test::MockSemanticsProvider provider(context_.get(), view_id);
  // Make sure the provider has finished connecting to the root.
  RunLoopUntilIdle();

  int number_of_nodes = 8;
  InitializeSemanticProvider(number_of_nodes, &provider);

  std::string result = tree_.LogSemanticTree(view_id_to_search);
  EXPECT_STREQ("", result.c_str());
}

}  // namespace a11y_manager_test
