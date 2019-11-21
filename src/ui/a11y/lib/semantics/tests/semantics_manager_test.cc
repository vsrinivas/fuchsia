// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/semantics_manager.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/zx/event.h>

#include <vector>

#include <gtest/gtest.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/semantic_tree_parser.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {
using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::NodePtr;
using fuchsia::accessibility::semantics::Role;
using fuchsia::accessibility::semantics::SemanticsManager;

const std::string kLabelA = "Label A";
const std::string kSemanticTreeSingle = "Node_id: 0, Label:Node-0\n";
const std::string kSemanticTreeOdd =
    "Node_id: 0, Label:Node-0\n"
    "    Node_id: 1, Label:Node-1\n"
    "        Node_id: 3, Label:Node-3\n"
    "        Node_id: 4, Label:Node-4\n"
    "    Node_id: 2, Label:Node-2\n"
    "        Node_id: 5, Label:Node-5\n"
    "        Node_id: 6, Label:Node-6\n";
const std::string kSemanticTreeEven =
    "Node_id: 0, Label:Node-0\n"
    "    Node_id: 1, Label:Node-1\n"
    "        Node_id: 3, Label:Node-3\n"
    "            Node_id: 7, Label:Node-7\n"
    "        Node_id: 4, Label:Node-4\n"
    "    Node_id: 2, Label:Node-2\n"
    "        Node_id: 5, Label:Node-5\n"
    "        Node_id: 6, Label:Node-6\n";
const int kMaxLogBufferSize = 1024;
const int kDeleteNodeId = 2;

const std::string kSemanticTreeSingleNodePath = "/pkg/data/semantic_tree_single_node.json";
const std::string kSemanticTreeOddNodesPath = "/pkg/data/semantic_tree_odd_nodes.json";
const std::string kSemanticTreeEvenNodesPath = "/pkg/data/semantic_tree_even_nodes.json";
const std::string kCyclicSemanticTreePath = "/pkg/data/cyclic_semantic_tree.json";
const std::string kDeletedSemanticSubtreePath = "/pkg/data/deleted_subtree_even_nodes.json";
const std::string kMultipleSubtreePath = "/pkg/data/deleted_subtree_even_nodes.json";

// Unit tests for src/ui/a11y/lib/semantics_manager.h and
// semantic_tree.h
class SemanticsManagerTest : public gtest::TestLoopFixture {
 public:
  SemanticsManagerTest() : semantics_manager_(context_provider_.context()) { syslog::InitLogger(); }

  static Node CreateTestNode(uint32_t node_id, std::string label);
  void InitializeActionListener(const std::string &file_path,
                                accessibility_test::MockSemanticProvider *provider);
  static int OpenAsFD(vfs::internal::Node *node, async_dispatcher_t *dispatcher);
  static char *ReadFile(vfs::internal::Node *node, int length, char *buffer);

  vfs::PseudoDir *debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

  sys::testing::ComponentContextProvider context_provider_;
  a11y::SemanticsManager semantics_manager_;
  SemanticTreeParser semantic_tree_parser_;
};

// Create a test node with only a node id and a label.
Node SemanticsManagerTest::CreateTestNode(uint32_t node_id, std::string label) {
  Node node = Node();
  node.set_node_id(node_id);
  node.set_child_ids({});
  node.set_role(Role::UNKNOWN);
  node.set_attributes(Attributes());
  node.mutable_attributes()->set_label(std::move(label));
  fuchsia::ui::gfx::BoundingBox box;
  node.set_location(box);
  fuchsia::ui::gfx::mat4 transform;
  node.set_transform(transform);
  return node;
}

void SemanticsManagerTest::InitializeActionListener(
    const std::string &file_path, accessibility_test::MockSemanticProvider *provider) {
  // Create Node List for the current semantic provider.
  std::vector<Node> nodes;
  ASSERT_TRUE(semantic_tree_parser_.ParseSemanticTree(file_path, &nodes));

  // Add nodes list to the current semantic providers list.
  provider->UpdateSemanticNodes(std::move(nodes));
  RunLoopUntilIdle();

  // Commit the nodes.
  provider->CommitUpdates();
  RunLoopUntilIdle();
}

int SemanticsManagerTest::OpenAsFD(vfs::internal::Node *node, async_dispatcher_t *dispatcher) {
  zx::channel local, remote;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &local, &remote));
  EXPECT_EQ(ZX_OK, node->Serve(fuchsia::io::OPEN_RIGHT_READABLE, std::move(remote), dispatcher));
  int fd = -1;
  EXPECT_EQ(ZX_OK, fdio_fd_create(local.release(), &fd));
  return fd;
}

char *SemanticsManagerTest::ReadFile(vfs::internal::Node *node, int length, char *buffer) {
  EXPECT_LE(length, kMaxLogBufferSize);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("ReadingDebugFile");

  int fd = OpenAsFD(node, loop.dispatcher());
  EXPECT_LE(0, fd);

  memset(buffer, 0, kMaxLogBufferSize);
  EXPECT_EQ(length, pread(fd, buffer, length, 0));
  return buffer;
}

// Basic test to check that a node update without commit will not result in
// any change to semantic tree.
TEST_F(SemanticsManagerTest, NodeUpdateWithoutCommit) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Creating test node to update.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, kLabelA);
  Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));

  // Update the node created above.
  semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Check that the node is not present in the tree.
  EXPECT_EQ(nullptr, semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0));
}

// Basic test to check that a node update with commit will result in
// node being changed in the tree.
TEST_F(SemanticsManagerTest, NodeUpdateWithCommit) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Creating test node to update.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, kLabelA);
  Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));

  // Update the node created above.
  semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_provider.CommitUpdates();
  RunLoopUntilIdle();

  // Check that the committed node is present in the semantic tree.
  NodePtr returned_node = semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
  EXPECT_NE(returned_node, nullptr);
  EXPECT_EQ(node.node_id(), returned_node->node_id());
  EXPECT_STREQ(node.attributes().label().data(), returned_node->attributes().label().data());
}

// Basic test to check that a node delete without commit should result in
// node not being deleted in the tree.
TEST_F(SemanticsManagerTest, NodeDeleteWithoutCommit) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Creating test node to update.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, kLabelA);
  Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));

  // Update the node created above.
  semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_provider.CommitUpdates();
  RunLoopUntilIdle();

  // Call Delete Node.
  std::vector<uint32_t> delete_nodes;
  delete_nodes.push_back(node.node_id());
  semantic_provider.DeleteSemanticNodes(std::move(delete_nodes));
  RunLoopUntilIdle();

  // Node should still be present.
  NodePtr returned_node = semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
  EXPECT_NE(returned_node, nullptr);
  EXPECT_EQ(node.node_id(), returned_node->node_id());
  EXPECT_STREQ(node.attributes().label().data(), returned_node->attributes().label().data());
}

// Basic test to check that a node delete with commit should result in
// node being deleted in the tree.
TEST_F(SemanticsManagerTest, NodeDeleteWithCommit) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Creating test node to update.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, kLabelA);
  Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));

  // Update the node created above.
  semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_provider.CommitUpdates();
  RunLoopUntilIdle();

  // Call Delete Node with commit.
  std::vector<uint32_t> delete_nodes;
  delete_nodes.push_back(node.node_id());
  semantic_provider.DeleteSemanticNodes(std::move(delete_nodes));
  semantic_provider.CommitUpdates();
  RunLoopUntilIdle();

  // Check that the node is not present in the tree.
  EXPECT_EQ(nullptr, semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0));
}

// CommitUpdates() should ensure that there are no cycles in the tree after
// Update/Delete has been applied. If they are present, the tree should be
// deleted.
// CommitUpdates should also delete the channel for this particular tree.
TEST_F(SemanticsManagerTest, DetectCycleInCommit) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  {
    // Create Semantic Tree;
    std::vector<Node> nodes_list;
    ASSERT_TRUE(semantic_tree_parser_.ParseSemanticTree(kCyclicSemanticTreePath, &nodes_list));

    std::vector<Node> nodes_list_copy;
    nodes_list_copy = fidl::Clone(nodes_list);

    // Call update on the newly created semantic tree with cycle.
    // Update the node created above.
    semantic_provider.UpdateSemanticNodes(std::move(nodes_list));
    RunLoopUntilIdle();

    // Commit nodes.
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Verify that Commit Called the callback on SemanticProvider.
    RunLoopUntilIdle();
    EXPECT_TRUE(semantic_provider.CommitFailedStatus());

    // Check that nodes are not present in the semantic tree.
    for (const Node &node : nodes_list_copy) {
      // Check that the node is not present in the tree.
      EXPECT_EQ(nullptr, semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(),
                                                                 node.node_id()));
    }
  }

  // Now since the channel is closed, Applying any more updates/commits should have no effect using
  // the same handle.
  {
    // Create Semantic Tree;
    std::vector<Node> nodes_list;
    ASSERT_TRUE(semantic_tree_parser_.ParseSemanticTree(kSemanticTreeEvenNodesPath, &nodes_list));

    std::vector<Node> nodes_list_copy;
    nodes_list_copy = fidl::Clone(nodes_list);

    // Call update on the newly created semantic tree without cycle.
    // Update the node created above.

    FXL_LOG(ERROR) << "Following Error message is expected since UpdateSemanticNodes call is made "
                      "on a channel which is closed.";
    semantic_provider.UpdateSemanticNodes(std::move(nodes_list));
    RunLoopUntilIdle();

    // Commit nodes.
    FXL_LOG(ERROR) << "Following Error message is expected since CommitUpdates call is made "
                      "on a channel which is closed.";
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Check that nodes are not present in the semantic tree.
    for (const Node &node : nodes_list_copy) {
      // Check that the node is not present in the tree.
      EXPECT_EQ(nullptr, semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(),
                                                                 node.node_id()));
    }
  }
}

// CommitUpdates() should ensure that there are no nodes without a parent.
// If there is a delete for a node, then semantic provider should ensure that all the nodes in that
// subtree are also deleted.
TEST_F(SemanticsManagerTest, ImproperDelete) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Create Semantic Tree;
  std::vector<Node> nodes_list;
  ASSERT_TRUE(semantic_tree_parser_.ParseSemanticTree(kSemanticTreeEvenNodesPath, &nodes_list));

  {  // Call update on the newly created semantic tree with cycle.
    // Update the node created above.
    semantic_provider.UpdateSemanticNodes(std::move(nodes_list));

    // Commit nodes.
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Check that commit was successful.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_NE(returned_node, nullptr);
  }
  {
    // Delete a node.
    std::vector<uint32_t> delete_nodes;
    delete_nodes.push_back(kDeleteNodeId);
    semantic_provider.DeleteSemanticNodes(std::move(delete_nodes));

    // Commit nodes.
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Check root node is not present, since we tried to push an invalid update/delete and commit
    // should catch the issue.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_EQ(returned_node, nullptr);
  }
}

// Checks when update with multuiple subtrees are sent then commit fails.
TEST_F(SemanticsManagerTest, MultipleSubtree) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Create Semantic Tree;
  std::vector<Node> nodes_list;
  ASSERT_TRUE(semantic_tree_parser_.ParseSemanticTree(kMultipleSubtreePath, &nodes_list));

  // Call update on the newly created semantic tree with multiple subtree.
  semantic_provider.UpdateSemanticNodes(std::move(nodes_list));

  // Commit nodes.
  semantic_provider.CommitUpdates();
  RunLoopUntilIdle();

  NodePtr returned_node = semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
  EXPECT_EQ(returned_node, nullptr);
}

// Checks that commit fails when a node is present in the update which doesn't have a parent.
TEST_F(SemanticsManagerTest, NodeWithoutParent) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Create Semantic Tree;
  std::vector<Node> nodes_list;
  ASSERT_TRUE(semantic_tree_parser_.ParseSemanticTree(kSemanticTreeEvenNodesPath, &nodes_list));

  // Call update on the newly created semantic tree with multiple subtree.
  semantic_provider.UpdateSemanticNodes(std::move(nodes_list));

  // Commit nodes.
  semantic_provider.CommitUpdates();
  RunLoopUntilIdle();

  // Make sure tree is formed and commit doesn't fail.
  {
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_NE(returned_node, nullptr);
  }

  // Send a node update which doesn't currently exist in the semantic tree and is not associated
  // with any parent node.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(100, "Label-100");
  Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));
  semantic_provider.UpdateSemanticNodes(std::move(update_nodes));

  // Commit Updates.
  semantic_provider.CommitUpdates();
  RunLoopUntilIdle();

  {
    // Check that commit fails and semantic tree is deleted.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_EQ(returned_node, nullptr);
  }
}

// Update()/Delete(): These operations should happen in the order in
// which these request came.
// For example: Update 1, data 1
//              delete 1
//              update 1, data 2
// should result in Update 1 , data2 and NOT Empty Tree.
TEST_F(SemanticsManagerTest, InOrderUpdatesAndDelete) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Update Node 0 to Label-A
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, "Label-A");
  Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));
  semantic_provider.UpdateSemanticNodes(std::move(update_nodes));

  // Delete Node 0.
  std::vector<uint32_t> delete_nodes;
  int delete_node_id = 0;
  delete_nodes.push_back(delete_node_id);
  semantic_provider.DeleteSemanticNodes(std::move(delete_nodes));

  // Update Node 0 to Label-B
  std::vector<Node> update_nodes2;
  Node node2 = CreateTestNode(0, "Label-B");
  Node clone_node2;
  node2.Clone(&clone_node2);
  update_nodes2.push_back(std::move(clone_node2));
  semantic_provider.UpdateSemanticNodes(std::move(update_nodes2));

  // Commit nodes.
  semantic_provider.CommitUpdates();
  RunLoopUntilIdle();

  // Check Node 0 is present and has Label-B.
  NodePtr returned_node = semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
  EXPECT_NE(returned_node, nullptr);
  EXPECT_EQ(node2.node_id(), returned_node->node_id());
  EXPECT_STREQ(node2.attributes().label().data(), returned_node->attributes().label().data());
}

// Test for LogSemanticTree() to make sure correct logs are generated,
// when number of nodes in the tree are odd.
TEST_F(SemanticsManagerTest, LogSemanticTree_OddNumberOfNodes) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  InitializeActionListener(kSemanticTreeOddNodesPath, &semantic_provider);
  vfs::internal::Node *node;
  EXPECT_EQ(ZX_OK, debug_dir()->Lookup(std::to_string(a11y::GetKoid(semantic_provider.view_ref())),
                                       &node));

  char buffer[kMaxLogBufferSize];
  ReadFile(node, kSemanticTreeOdd.size(), buffer);
  EXPECT_EQ(kSemanticTreeOdd, buffer);
}

// Test for LogSemanticTree() to make sure correct logs are generated,
// when number of nodes in the tree are even.
TEST_F(SemanticsManagerTest, LogSemanticTree_EvenNumberOfNodes) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  InitializeActionListener(kSemanticTreeEvenNodesPath, &semantic_provider);
  vfs::internal::Node *node;
  EXPECT_EQ(ZX_OK, debug_dir()->Lookup(std::to_string(a11y::GetKoid(semantic_provider.view_ref())),
                                       &node));

  char buffer[kMaxLogBufferSize];
  ReadFile(node, kSemanticTreeEven.size(), buffer);
  EXPECT_EQ(kSemanticTreeEven, buffer);
}

// Test for LogSemanticTree() to make sure correct logs are generated,
// when there is just a single node in the tree for a particular view.
TEST_F(SemanticsManagerTest, LogSemanticTree_SingleNode) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  InitializeActionListener(kSemanticTreeSingleNodePath, &semantic_provider);
  vfs::internal::Node *node;
  EXPECT_EQ(ZX_OK, debug_dir()->Lookup(std::to_string(a11y::GetKoid(semantic_provider.view_ref())),
                                       &node));

  char buffer[kMaxLogBufferSize];
  ReadFile(node, kSemanticTreeSingle.size(), buffer);
  EXPECT_EQ(kSemanticTreeSingle, buffer);
}

// Test that calls LogSemanticTreeForView() with an unregistered ViewRef returns empty string.
TEST_F(SemanticsManagerTest, LogSemanticTreeForView_NoViewRegistered) {
  fuchsia::ui::views::ViewRef unregistered_view_ref;
  zx::eventpair unused;

  FX_CHECK(zx::eventpair::create(0u, &unregistered_view_ref.reference, &unused) == ZX_OK);

  std::string semantic_tree_log = semantics_manager_.LogSemanticTreeForView(unregistered_view_ref);
  EXPECT_TRUE(semantic_tree_log.empty());
}

// Test for LogSemanticTreeForView() to make sure correct logs are generated,
// when there are multiple views registered with the semantics manager.
TEST_F(SemanticsManagerTest, LogSemanticTreeForView_MultipleViews) {
  // Create fake semantic provider and register view with semantics_manager_.
  accessibility_test::MockSemanticProvider semantic_provider_single_node(&semantics_manager_);

  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Update tree for fake view.
  InitializeActionListener(kSemanticTreeSingleNodePath, &semantic_provider_single_node);

  // Add a second view and update its tree.
  accessibility_test::MockSemanticProvider semantic_provider_odd_nodes(&semantics_manager_);

  RunLoopUntilIdle();

  InitializeActionListener(kSemanticTreeOddNodesPath, &semantic_provider_odd_nodes);

  // Verify that LogSemanticTreeForView() returns the correct logs when supplied each of the
  // two registered view refs.
  std::string single_node_tree_log =
      semantics_manager_.LogSemanticTreeForView(semantic_provider_single_node.view_ref());
  EXPECT_EQ(single_node_tree_log, kSemanticTreeSingle);

  std::string odd_nodes_tree_log =
      semantics_manager_.LogSemanticTreeForView(semantic_provider_odd_nodes.view_ref());
  EXPECT_EQ(odd_nodes_tree_log, kSemanticTreeOdd);
}

// Test for PerformHitTesting() to make sure correct node_id is passed from the
// semantic provider to semantics manager.
TEST_F(SemanticsManagerTest, PerformHitTesting_Pass) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  InitializeActionListener(kSemanticTreeOddNodesPath, &semantic_provider);

  // Set HitTest result in action listener.
  static constexpr uint32_t expected_result = 5;
  semantic_provider.SetHitTestResult(expected_result);

  zx_koid_t koid = a11y::GetKoid(semantic_provider.view_ref());
  ::fuchsia::math::PointF local_point;
  fuchsia::accessibility::semantics::Hit hit;
  semantics_manager_.PerformHitTesting(koid, local_point,
                                       [&hit](fuchsia::accessibility::semantics::Hit received_hit) {
                                         hit = std::move(received_hit);
                                       });

  RunLoopUntilIdle();
  EXPECT_EQ(expected_result, hit.node_id());
  EXPECT_EQ(1ul, hit.path_from_root().size());
  EXPECT_EQ(expected_result, hit.path_from_root()[0]);
}

// Basic test to make sure nodes can be searched using node id and Koid of
// ViewRef of that semantic tree.
TEST_F(SemanticsManagerTest, GetAccessibilityNodeByKoid) {
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Creating test node to update.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, kLabelA);
  Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));

  // Update the node created above.
  semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_provider.CommitUpdates();
  RunLoopUntilIdle();

  // Check that the committed node is present in the semantic tree.
  zx_koid_t koid = a11y::GetKoid(semantic_provider.view_ref());
  NodePtr returned_node = semantics_manager_.GetAccessibilityNodeByKoid(koid, 0);
  EXPECT_NE(returned_node, nullptr);
  EXPECT_EQ(node.node_id(), returned_node->node_id());
  EXPECT_STREQ(node.attributes().label().data(), returned_node->attributes().label().data());
}

// Basic test for partial node updates.
TEST_F(SemanticsManagerTest, PartialNodeUpdateWithCommit) {
  // Create ActionListener.
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Creating test node to update.
  {
    std::vector<Node> update_nodes;
    Node node = CreateTestNode(0, kLabelA);
    Node clone_node;
    node.Clone(&clone_node);
    update_nodes.push_back(std::move(clone_node));

    // Update the node created above.
    semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();
    update_nodes.clear();

    // Commit nodes.
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Check that the committed node is present in the semantic tree.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_NE(returned_node, nullptr);
    EXPECT_EQ(node.node_id(), returned_node->node_id());
    EXPECT_STREQ(node.attributes().label().data(), returned_node->attributes().label().data());
  }

  // Send a partial update by adding a new field, and ensure new field is added while previous
  // fields are retained.
  {
    std::vector<Node> update_nodes;
    Node node = CreateTestNode(0, kLabelA);
    Node partial_node = Node();
    partial_node.set_node_id(0);
    partial_node.set_child_ids({1, 2});
    Node clone_partial_node;
    partial_node.Clone(&clone_partial_node);
    update_nodes.push_back(std::move(clone_partial_node));

    // Send nodes 1 and 2 otherwise, commit will fail.
    {
      Node child_node = CreateTestNode(1, kLabelA);
      Node clone_node;
      child_node.Clone(&clone_node);
      update_nodes.push_back(std::move(clone_node));
    }

    {
      Node child_node = CreateTestNode(2, kLabelA);
      Node clone_node;
      child_node.Clone(&clone_node);
      update_nodes.push_back(std::move(clone_node));
    }

    // Update the node created above.
    semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();

    // Commit nodes.
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Check that the committed node is present in the semantic tree.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_NE(returned_node, nullptr);
    EXPECT_EQ(node.node_id(), returned_node->node_id());
    EXPECT_STREQ(node.attributes().label().data(), returned_node->attributes().label().data());
    ASSERT_TRUE(returned_node->has_child_ids());
    EXPECT_EQ(returned_node->child_ids(), partial_node.child_ids());
  }
}

// Test for checking that Partial node updates are not applied if node-id is missing.
TEST_F(SemanticsManagerTest, PartialNodeUpdateWithCommit_NodeIdMissing) {
  // Create ActionListener.
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Creating test node to update.
  {
    std::vector<Node> update_nodes;
    Node node = CreateTestNode(0, kLabelA);
    Node clone_node;
    node.Clone(&clone_node);
    update_nodes.push_back(std::move(clone_node));

    // Update the node created above.
    semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();
    update_nodes.clear();

    // Commit nodes.
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Check that the committed node is present in the semantic tree.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_NE(returned_node, nullptr);
    EXPECT_EQ(node.node_id(), returned_node->node_id());
    EXPECT_STREQ(node.attributes().label().data(), returned_node->attributes().label().data());
  }

  // Send a partial update by adding a new field, and ensure node is unchanged.
  {
    std::vector<Node> update_nodes;
    Node node = CreateTestNode(0, kLabelA);
    Node partial_node = Node();
    partial_node.set_child_ids({1, 2});
    Node clone_partial_node;
    partial_node.Clone(&clone_partial_node);
    update_nodes.push_back(std::move(clone_partial_node));

    // Update the node created above.
    semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();

    // Commit nodes.
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Check that the node is not updated.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_NE(returned_node, nullptr);
    EXPECT_EQ(node.node_id(), returned_node->node_id());
    EXPECT_STREQ(node.attributes().label().data(), returned_node->attributes().label().data());
    ASSERT_TRUE(returned_node->has_child_ids());
    EXPECT_NE(returned_node->child_ids(), partial_node.child_ids());
  }
}

// Basic test to check that Semantic Provider gets notified and checks that semantic tree is
// deleted inside A11y when Semantics Manager is disabled.
TEST_F(SemanticsManagerTest, SemanticsManagerDisabled) {
  // Enable Semantics Manager.
  semantics_manager_.SetSemanticsManagerEnabled(true);
  RunLoopUntilIdle();

  // Create SemanticListener.

  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // On registration of a new view, Semantic Listener should get notified about the current
  // settings.
  EXPECT_TRUE(semantic_provider.GetSemanticsEnabled());

  // Add a node to semantic tree.
  {
    std::vector<Node> update_nodes;
    Node node = CreateTestNode(0, kLabelA);
    Node clone_node;
    node.Clone(&clone_node);
    update_nodes.push_back(std::move(clone_node));

    // Update the node created above.
    semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();
    update_nodes.clear();

    // Commit nodes.
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Check that the committed node is present in the semantic tree.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_NE(returned_node, nullptr);
    EXPECT_EQ(node.node_id(), returned_node->node_id());
    EXPECT_STREQ(node.attributes().label().data(), returned_node->attributes().label().data());
  }

  // Disable Semantics Manager.
  semantics_manager_.SetSemanticsManagerEnabled(false);
  RunLoopUntilIdle();
  // Semantics Listener should get notified about Semantics manager disable.
  EXPECT_FALSE(semantic_provider.GetSemanticsEnabled());

  // Check that semantic tree is empty.
  NodePtr returned_node = semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
  EXPECT_EQ(returned_node, nullptr);
}

// Test to check that when event pair is signalled, Semantics Manager gets the signal and it closes
// the fidl channel.
TEST_F(SemanticsManagerTest, EventPairSignalled) {
  // Enable Semantics Manager.
  semantics_manager_.SetSemanticsManagerEnabled(true);

  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  RunLoopUntilIdle();

  // On registration of a new view, Semantic Listener should get notified about the current
  // settings.
  EXPECT_TRUE(semantic_provider.GetSemanticsEnabled());

  semantic_provider.SendEventPairSignal();
  RunLoopUntilIdle();

  // Send node updates and commits.
  {
    std::vector<Node> update_nodes;
    Node node = CreateTestNode(0, kLabelA);
    Node clone_node;
    node.Clone(&clone_node);
    update_nodes.push_back(std::move(clone_node));

    // Update the node created above.
    FXL_LOG(ERROR) << "Following Error is expected since UpdateSemanticNodes call is made on a "
                      "channel which is closed.";
    semantic_provider.UpdateSemanticNodes(std::move(update_nodes));

    // Commit nodes.
    FXL_LOG(ERROR) << "Following Error is expected since CommitUpdates call is made on a "
                      "channel which is closed.";
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Check that the committed node is not present in the semantic tree.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_EQ(returned_node, nullptr);
  }
}

// This test ensures that empty trees are supported in semantics manager by:
//   1. Adding a single node to the tree and expect commit to be successful.
//   2. Delete the only node present in the semantic tree.
//   3. Add another node to the tree, to make sure previous delete did not close the channel(since
//   empty tree is a valid tree and commit should not fail in this situation.)
TEST_F(SemanticsManagerTest, EmptyTreeIsSupported) {
  // Create Semantic Provider.
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Create test node to update.
  Node root_node = CreateTestNode(0, kLabelA);
  // Commit newly created node.
  {
    std::vector<Node> update_nodes;
    Node clone_node;
    root_node.Clone(&clone_node);
    update_nodes.push_back(std::move(clone_node));

    // Update the node created above.
    semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();

    // Commit Node.
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Check that the root node is present in the tree.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_NE(returned_node, nullptr);
    EXPECT_EQ(root_node.node_id(), returned_node->node_id());
    EXPECT_STREQ(root_node.attributes().label().data(), returned_node->attributes().label().data());
  }

  // Call delete node for the "root_node" added above.
  {
    std::vector<uint32_t> delete_nodes;
    delete_nodes.push_back(root_node.node_id());
    semantic_provider.DeleteSemanticNodes(std::move(delete_nodes));
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();
    // Check that the root node is not present in the tree.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_EQ(returned_node, nullptr);
  }

  // Make sure channel is open.
  EXPECT_FALSE(semantic_provider.IsChannelClosed());
}

// Tests that adding nodes without a root node will result in failed commit.
TEST_F(SemanticsManagerTest, AddingNodeWithoutRoot) {
  // Create Semantic Provider.
  accessibility_test::MockSemanticProvider semantic_provider(&semantics_manager_);
  // We make sure the Semantic Action Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Create non-root node to update.
  Node node = CreateTestNode(1, kLabelA);
  // Commit newly created node.
  {
    std::vector<Node> update_nodes;
    Node clone_node;
    node.Clone(&clone_node);
    update_nodes.push_back(std::move(clone_node));

    // Update the node created above.
    semantic_provider.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();

    // Commit Node.
    semantic_provider.CommitUpdates();
    RunLoopUntilIdle();

    // Check that the node is not present in the tree since trying to add a node without a root node
    // should result in Commit() failure.
    NodePtr returned_node =
        semantics_manager_.GetAccessibilityNode(semantic_provider.view_ref(), 0);
    EXPECT_EQ(returned_node, nullptr);

    // Make sure channel is closed.
    EXPECT_TRUE(semantic_provider.IsChannelClosed());
  }
}

}  // namespace accessibility_test
