// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/semantic_tree.h"

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
#include "src/ui/a11y/lib/semantics/semantics_manager.h"
#include "src/ui/a11y/lib/semantics/tests/semantic_tree_parser.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;
using fuchsia::accessibility::semantics::SemanticTree;

const std::string kRootNodeNotFound = "Root Node not found.";
const std::string kSemanticTreeSingle = "Node_id: 0, Label:Node-0\n";
const std::string kSemanticTreeOdd =
    "Node_id: 0, Label:Node-0\n"
    "    Node_id: 1, Label:Node-1\n"
    "        Node_id: 3, Label:Node-3\n"
    "        Node_id: 4, Label:Node-4\n"
    "    Node_id: 2, Label:Node-2\n"
    "        Node_id: 5, Label:Node-5\n"
    "        Node_id: 6, Label:Node-6\n";
const std::string kSemanticTreeOddDeletedNode =
    "Node_id: 0, Label:Node-0\n"
    "    Node_id: 1, Label:Node-1\n"
    "        Node_id: 3, Label:Node-3\n"
    "        Node_id: 4, Label:Node-4\n"
    "    Node_id: 2, Label:Node-2\n"
    "        Node_id: 5, Label:Node-5\n";
const std::string kSemanticTreeOddUpdatedNode =
    "Node_id: 0, Label:Node-0\n"
    "    Node_id: 1, Label:Node-1-updated\n"
    "        Node_id: 3, Label:Node-3\n"
    "        Node_id: 4, Label:Node-4\n"
    "    Node_id: 2, Label:Node-2\n"
    "        Node_id: 5, Label:Node-5\n"
    "        Node_id: 6, Label:Node-6\n";

const std::string kSemanticTreeSingleNodePath = "/pkg/data/semantic_tree_single_node.json";
const std::string kSemanticTreeOddNodesPath = "/pkg/data/semantic_tree_odd_nodes.json";

// Unit tests for src/ui/a11y/lib/semantics_manager.h and
// semantic_tree.h
class SemanticTreeTest : public gtest::TestLoopFixture {
 public:
  SemanticTreeTest() { syslog::InitLogger(); }

 protected:
  void SetUp() override {
    TestLoopFixture::SetUp();
    zx::eventpair a, b;
    zx::eventpair::create(0u, &a, &b);
    view_ref_ = fuchsia::ui::views::ViewRef({
        .reference = std::move(a),
    });
  }

  fuchsia::ui::views::ViewRef Clone(const fuchsia::ui::views::ViewRef &view_ref) {
    fuchsia::ui::views::ViewRef clone;
    FX_CHECK(fidl::Clone(view_ref, &clone) == ZX_OK);
    return clone;
  }

  static Node CreateTestNode(uint32_t node_id, std::string label,
                             const std::vector<uint32_t> child_ids);
  void InitializeTreeNodesFromFile(const std::string &file_path, a11y::SemanticTree *semantic_tree);

  vfs::PseudoDir *debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

  fuchsia::ui::views::ViewRef view_ref_;
  sys::testing::ComponentContextProvider context_provider_;
  SemanticTreeParser semantic_tree_parser_;
};

// Create a test node with only a node id and a label.
Node SemanticTreeTest::CreateTestNode(uint32_t node_id, std::string label,
                                      const std::vector<uint32_t> child_ids) {
  Node node = Node();
  node.set_node_id(node_id);
  node.mutable_attributes()->set_label(std::move(label));
  node.set_child_ids(child_ids);
  return node;
}

void SemanticTreeTest::InitializeTreeNodesFromFile(const std::string &file_path,
                                                   a11y::SemanticTree *semantic_tree) {
  // Create Node List for the current semantic provider.
  std::vector<Node> nodes;
  ASSERT_TRUE(semantic_tree_parser_.ParseSemanticTree(file_path, &nodes));

  // Add nodes list to the current semantic providers list.
  semantic_tree->InitializeNodesForTest(std::move(nodes));
}

// Basic test that LogSemanticTree() produces expected output for tree with single node.
TEST_F(SemanticTreeTest, LogSemanticTreeSingleNode) {
  a11y::SemanticTree::CloseChannelCallback close_channel_callback([](zx_koid_t /*unused*/) {});

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  InitializeTreeNodesFromFile(kSemanticTreeSingleNodePath, &semantic_tree);

  EXPECT_EQ(semantic_tree.LogSemanticTree(), kSemanticTreeSingle);
}

// Verify that GetAccessibilityNode() returns nullptr if node_id is not found.
TEST_F(SemanticTreeTest, GetAccessibilityNodeIdNotFound) {
  a11y::SemanticTree::CloseChannelCallback close_channel_callback([](zx_koid_t /*unused*/) {});

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  InitializeTreeNodesFromFile(kSemanticTreeSingleNodePath, &semantic_tree);

  // Attempt to retrieve node with id not present in tree.
  auto returned_node_ptr = semantic_tree.GetAccessibilityNode(1u);

  EXPECT_EQ(returned_node_ptr, nullptr);
}

// Verify that GetAccessibilityNode() returns correct node if node_id is found.
TEST_F(SemanticTreeTest, GetAccessibilityNodeIdFound) {
  a11y::SemanticTree::CloseChannelCallback close_channel_callback([](zx_koid_t /*unused*/) {});

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  InitializeTreeNodesFromFile(kSemanticTreeSingleNodePath, &semantic_tree);

  // Attempt to retrieve node with id not present in tree.
  auto returned_node_ptr = semantic_tree.GetAccessibilityNode(0u);

  ASSERT_NE(returned_node_ptr, nullptr);
  EXPECT_EQ(returned_node_ptr->node_id(), 0u);
}

// Verify that DeleteSemanticNodes() correctly populates pending_transactions_.
TEST_F(SemanticTreeTest, DeleteSemanticNodesPopulatesPendingTransactions) {
  a11y::SemanticTree::CloseChannelCallback close_channel_callback([](zx_koid_t /*unused*/) {});

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  InitializeTreeNodesFromFile(kSemanticTreeOddNodesPath, &semantic_tree);

  semantic_tree.DeleteSemanticNodes({1u, 2u});

  // Verify that pending_transactions_ now contains deletion transactions for nodes 1 and 2.
  auto pending_deletions = semantic_tree.GetPendingDeletions();
  auto pending_updates = semantic_tree.GetPendingUpdates();

  EXPECT_EQ(pending_deletions.size(), 2u);
  EXPECT_EQ(pending_deletions[0], 1u);
  EXPECT_EQ(pending_deletions[1], 2u);
  EXPECT_TRUE(pending_updates.empty());
}

// Verify that UpdateSemanticNodes() correctly populates pending_transactions_.
TEST_F(SemanticTreeTest, UpdateSemanticNodesPopulatesPendingTransactions) {
  a11y::SemanticTree::CloseChannelCallback close_channel_callback([](zx_koid_t /*unused*/) {});

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  InitializeTreeNodesFromFile(kSemanticTreeOddNodesPath, &semantic_tree);

  std::vector<Node> node_updates;

  // Partial update -- only id and label should be affected in transaction.
  Node node_update_0 = Node();
  node_update_0.set_node_id(0u);
  node_update_0.mutable_attributes()->set_label("Node-0-updated");
  node_updates.push_back(std::move(node_update_0));

  // No node id -- no transaction should be created.
  Node node_update_1 = Node();
  node_update_1.mutable_attributes()->set_label("Node-1-updated");
  node_updates.push_back(std::move(node_update_1));

  // New node id -- transaction should be created, and should include full node.
  Node node_update_2 = Node();
  node_update_2.set_node_id(7u);
  node_update_2.mutable_attributes()->set_label("Node-7-updated");
  node_updates.push_back(std::move(node_update_2));

  semantic_tree.UpdateSemanticNodes(std::move(node_updates));

  // Verify that pending_transactions_ now contains deletion transactions for nodes 1 and 2.
  auto pending_deletions = semantic_tree.GetPendingDeletions();
  auto pending_updates = semantic_tree.GetPendingUpdates();

  EXPECT_EQ(pending_updates.size(), 2u);
  EXPECT_EQ(pending_updates[0].node_id(), 0u);
  EXPECT_EQ(pending_updates[0].attributes().label(), "Node-0-updated");
  EXPECT_TRUE(pending_updates[0].has_child_ids());
  EXPECT_EQ(pending_updates[0].child_ids().size(), 2u);

  EXPECT_EQ(pending_updates[1].node_id(), 7u);
  EXPECT_EQ(pending_updates[1].attributes().label(), "Node-7-updated");
  EXPECT_FALSE(pending_updates[1].has_child_ids());

  EXPECT_TRUE(pending_deletions.empty());
}

// Verify that CommitUpdates() applies pending valid node deletions.
TEST_F(SemanticTreeTest, CommitUpdatesAppliesPendingDeletion) {
  bool close_channel_callback_called = false;
  bool commit_updates_callback_called = false;

  a11y::SemanticTree::CloseChannelCallback close_channel_callback(
      [&close_channel_callback_called](zx_koid_t /*unused*/) {
        close_channel_callback_called = true;
      });

  a11y::SemanticTree::CommitUpdatesCallback commit_updates_callback(
      [&commit_updates_callback_called]() { commit_updates_callback_called = true; });

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  InitializeTreeNodesFromFile(kSemanticTreeOddNodesPath, &semantic_tree);

  // Add a pending transaction to delete node with id 6.
  semantic_tree.AddPendingTransaction(6u /*node_id*/, true /*delete_node*/,
                                      CreateTestNode(6u, "" /*label*/, {} /*child_ids*/));

  // Attempt to retrieve node with id not present in tree.
  semantic_tree.CommitUpdates(std::move(commit_updates_callback));

  EXPECT_EQ(semantic_tree.LogSemanticTree(), kSemanticTreeOddDeletedNode);
  EXPECT_TRUE(commit_updates_callback_called);
  EXPECT_FALSE(close_channel_callback_called);
}

// Verify that CommitUpdates() applies pending valid node updates.
TEST_F(SemanticTreeTest, CommitUpdatesAppliesPendingUpdates) {
  bool close_channel_callback_called = false;
  bool commit_updates_callback_called = false;

  a11y::SemanticTree::CloseChannelCallback close_channel_callback(
      [&close_channel_callback_called](zx_koid_t /*unused*/) {
        close_channel_callback_called = true;
      });

  a11y::SemanticTree::CommitUpdatesCallback commit_updates_callback(
      [&commit_updates_callback_called]() { commit_updates_callback_called = true; });

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  InitializeTreeNodesFromFile(kSemanticTreeOddNodesPath, &semantic_tree);

  // Add a pending transaction to update label on node with id 1.
  semantic_tree.AddPendingTransaction(
      1u /*node_id*/, false /*delete_node*/,
      CreateTestNode(1u, "Node-1-updated" /*label*/, {3u, 4u} /*child_ids*/));

  // Attempt to retrieve node with id not present in tree.
  semantic_tree.CommitUpdates(std::move(commit_updates_callback));

  EXPECT_EQ(semantic_tree.LogSemanticTree(), kSemanticTreeOddUpdatedNode);
  EXPECT_TRUE(commit_updates_callback_called);
  EXPECT_FALSE(close_channel_callback_called);
}

// Verify that CommitUpdates() clears tree and closes channel if root node is not found.
TEST_F(SemanticTreeTest, CommitUpdatesClearsTreeIfRootNotFound) {
  bool close_channel_callback_called = false;
  bool commit_updates_callback_called = false;

  a11y::SemanticTree::CloseChannelCallback close_channel_callback(
      [&close_channel_callback_called](zx_koid_t /*unused*/) {
        close_channel_callback_called = true;
      });

  a11y::SemanticTree::CommitUpdatesCallback commit_updates_callback(
      [&commit_updates_callback_called]() { commit_updates_callback_called = true; });

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  std::vector<Node> nodes;
  nodes.push_back(CreateTestNode(1u /*node_id*/, "Node-1" /*label*/, {} /*child_ids*/));
  semantic_tree.InitializeNodesForTest(std::move(nodes));

  // Add a pending transaction to update label on node with id 1.
  semantic_tree.AddPendingTransaction(
      1u /*node_id*/, false /*delete_node*/,
      CreateTestNode(1u /*node_id*/, "Node-1-updated" /*label*/, {} /*child_ids*/));

  // Attempt to retrieve node with id not present in tree.
  semantic_tree.CommitUpdates(std::move(commit_updates_callback));

  EXPECT_EQ(semantic_tree.LogSemanticTree(), kRootNodeNotFound);

  // Commit updates callback only called if ApplyCommit() returns false, and ApplyCommit() returns
  // false if root node is not found.
  EXPECT_TRUE(commit_updates_callback_called);
  EXPECT_TRUE(close_channel_callback_called);
}

// Verify that CommitUpdates() clears tree and closes channel if tree contains a cycle.
TEST_F(SemanticTreeTest, CommitUpdatesClearsTreeIfCycleFound) {
  bool close_channel_callback_called = false;
  bool commit_updates_callback_called = false;

  a11y::SemanticTree::CloseChannelCallback close_channel_callback(
      [&close_channel_callback_called](zx_koid_t /*unused*/) {
        close_channel_callback_called = true;
      });

  a11y::SemanticTree::CommitUpdatesCallback commit_updates_callback(
      [&commit_updates_callback_called]() { commit_updates_callback_called = true; });

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  std::vector<Node> nodes;
  nodes.push_back(CreateTestNode(0u /*node_id*/, "Node-0" /*label*/, {1u} /*child_ids*/));
  nodes.push_back(CreateTestNode(1u /*node_id*/, "Node-1" /*label*/, {2u} /*child_ids*/));
  nodes.push_back(CreateTestNode(2u /*node_id*/, "Node-2" /*label*/, {0u} /*child_ids*/));
  semantic_tree.InitializeNodesForTest(std::move(nodes));

  // Add a pending transaction to update label on node with id 1.
  semantic_tree.AddPendingTransaction(
      1u /*node_id*/, false /*delete_node*/,
      CreateTestNode(1u /*node_id*/, "Node-1-updated" /*label*/, {} /*child_ids*/));

  // Attempt to retrieve node with id not present in tree.
  semantic_tree.CommitUpdates(std::move(commit_updates_callback));

  EXPECT_EQ(semantic_tree.LogSemanticTree(), kRootNodeNotFound);

  // |commit_updates_callback| is always called to signal that the commit was
  // processed. If the tree was not well formed, |close_channel_callback| is
  // invoked to raise an error.
  EXPECT_TRUE(commit_updates_callback_called);
  EXPECT_TRUE(close_channel_callback_called);
}

// Verify that CommitUpdates() clears tree and closes channel if any nodes reference non-existent
// children in child_ids.
TEST_F(SemanticTreeTest, CommitUpdatesClearsTreeIfNonexistentChildIdFound) {
  bool close_channel_callback_called = false;
  bool commit_updates_callback_called = false;

  a11y::SemanticTree::CloseChannelCallback close_channel_callback(
      [&close_channel_callback_called](zx_koid_t /*unused*/) {
        close_channel_callback_called = true;
      });

  a11y::SemanticTree::CommitUpdatesCallback commit_updates_callback(
      [&commit_updates_callback_called]() { commit_updates_callback_called = true; });

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  std::vector<Node> nodes;
  nodes.push_back(CreateTestNode(0u /*node_id*/, "Node-0" /*label*/, {1u, 2u} /*child_ids*/));
  nodes.push_back(CreateTestNode(1u /*node_id*/, "Node-1" /*label*/, {3u} /*child_ids*/));
  nodes.push_back(
      CreateTestNode(2u /*node_id*/, "Node-2" /*label*/, {4u} /*child_ids*/));  // MALFORMED
  nodes.push_back(CreateTestNode(3u /*node_id*/, "Node-3" /*label*/, {} /*child_ids*/));
  semantic_tree.InitializeNodesForTest(std::move(nodes));

  // Add a pending transaction to update label on node with id 1.
  semantic_tree.AddPendingTransaction(
      1u /*node_id*/, false /*delete_node*/,
      CreateTestNode(1u /*node_id*/, "Node-1-updated" /*label*/, {} /*child_ids*/));

  // Attempt to retrieve node with id not present in tree.
  semantic_tree.CommitUpdates(std::move(commit_updates_callback));

  EXPECT_EQ(semantic_tree.LogSemanticTree(), kRootNodeNotFound);

  EXPECT_TRUE(commit_updates_callback_called);
  EXPECT_TRUE(close_channel_callback_called);
}

// Verify that CommitUpdates() clears tree and closes channel if tree contains unreachable nodes.
TEST_F(SemanticTreeTest, CommitUpdatesClearsTreeIfTreeContainsUnreachableNodes) {
  bool close_channel_callback_called = false;
  bool commit_updates_callback_called = false;

  a11y::SemanticTree::CloseChannelCallback close_channel_callback(
      [&close_channel_callback_called](zx_koid_t /*unused*/) {
        close_channel_callback_called = true;
      });

  a11y::SemanticTree::CommitUpdatesCallback commit_updates_callback(
      [&commit_updates_callback_called]() { commit_updates_callback_called = true; });

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  std::vector<Node> nodes;
  nodes.push_back(CreateTestNode(0u /*node_id*/, "Node-0" /*label*/, {1u} /*child_ids*/));
  nodes.push_back(CreateTestNode(1u /*node_id*/, "Node-1" /*label*/, {} /*child_ids*/));
  nodes.push_back(
      CreateTestNode(2u /*node_id*/, "Node-2" /*label*/, {} /*child_ids*/));  // UNREACHABLE
  semantic_tree.InitializeNodesForTest(std::move(nodes));

  // Add a pending transaction to update label on node with id 1.
  semantic_tree.AddPendingTransaction(
      1u /*node_id*/, false /*delete_node*/,
      CreateTestNode(1u /*node_id*/, "Node-1-updated" /*label*/, {} /*child_ids*/));

  // Attempt to retrieve node with id not present in tree.
  semantic_tree.CommitUpdates(std::move(commit_updates_callback));

  EXPECT_EQ(semantic_tree.LogSemanticTree(), kRootNodeNotFound);

  EXPECT_TRUE(commit_updates_callback_called);
  EXPECT_TRUE(close_channel_callback_called);
}

// Verify that IsSameView() returns true when supplied tree's view ref.
TEST_F(SemanticTreeTest, IsSameViewReturnsTrueForTreeViewRef) {
  a11y::SemanticTree::CloseChannelCallback close_channel_callback([](zx_koid_t /*unused*/) {});

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  EXPECT_TRUE(semantic_tree.IsSameView(Clone(view_ref_)));
}

// Verify that IsSameKoid() returns true when supplied koid corresponding to tree's view.
TEST_F(SemanticTreeTest, IsSameKoidReturnsTrueForTreeViewRef) {
  a11y::SemanticTree::CloseChannelCallback close_channel_callback([](zx_koid_t /*unused*/) {});

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  EXPECT_TRUE(semantic_tree.IsSameKoid(a11y::GetKoid(Clone(view_ref_))));
}

// Verify that disabling semantic updates clears tree.
TEST_F(SemanticTreeTest, EnableSemanticsUpdatesClearsTreeOnDisable) {
  a11y::SemanticTree::CloseChannelCallback close_channel_callback([](zx_koid_t /*unused*/) {});

  a11y::SemanticTree semantic_tree(
      Clone(view_ref_), fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
      debug_dir(), std::move(close_channel_callback));

  // Initialize test tree state.
  InitializeTreeNodesFromFile(kSemanticTreeSingleNodePath, &semantic_tree);

  EXPECT_EQ(semantic_tree.LogSemanticTree(), kSemanticTreeSingle);

  // Disable semantic updates and verify that tree is cleared.
  semantic_tree.EnableSemanticsUpdates(false);

  EXPECT_EQ(semantic_tree.LogSemanticTree(), kRootNodeNotFound);
}

}  // namespace accessibility_test
