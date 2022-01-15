// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <array>
#include <limits>
#include <random>

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/wavl_tree_best_node_observer.h>
#include <zxtest/zxtest.h>

namespace {

// The definition of the node we will use during testing.
struct TestNode {
  static constexpr uint32_t kInvalidAugmentedVal = std::numeric_limits<uint32_t>::max();

  TestNode() = default;
  ~TestNode() { ZX_ASSERT(subtree_best == kInvalidAugmentedVal); }

  uint32_t key{0};
  uint32_t augmented_val{0};
  uint32_t subtree_best{kInvalidAugmentedVal};
  fbl::WAVLTreeNodeState<TestNode*> tree_node;
};

// Traits used to locate the WAVL tree node state in TestNode, as well as to
// establish the sorting invariant.
struct TestNodeTraits {
  static uint32_t GetKey(const TestNode& test_node) { return test_node.key; }
  static bool LessThan(uint32_t a, uint32_t b) { return a < b; }
  static bool EqualTo(uint32_t a, uint32_t b) { return a == b; }
  static auto& node_state(TestNode& test_node) { return test_node.tree_node; }
};

// A base definition of the "best node" traits which defines "best" as the node
// with the minimum |augmented_val|.
struct BestNodeTraits {
  static uint32_t GetValue(const TestNode& node) { return node.augmented_val; }
  static uint32_t GetSubtreeBest(const TestNode& node) { return node.subtree_best; }
  static bool Compare(uint32_t a, uint32_t b) { return a < b; }
  static void AssignBest(TestNode& node, uint32_t val) { node.subtree_best = val; }
  static void ResetBest(TestNode& target) { target.subtree_best = TestNode::kInvalidAugmentedVal; }
};

using TestTree = fbl::WAVLTree<uint32_t, TestNode*, TestNodeTraits, fbl::DefaultObjectTag,
                               TestNodeTraits, fbl::WAVLTreeBestNodeObserver<BestNodeTraits>>;

template <typename NodeCollection>
void ValidateTree(const TestTree& tree, const NodeCollection& nodes,
                  const TestNode* extra_node = nullptr) {
  std::optional<uint32_t> best;
  auto UpdateBest = [&best](const TestNode& node) {
    if (node.tree_node.InContainer()) {
      ASSERT_NE(TestNode::kInvalidAugmentedVal, node.subtree_best);
      best = best.has_value() ? std::min(best.value(), node.augmented_val) : node.augmented_val;
    } else {
      ASSERT_EQ(TestNode::kInvalidAugmentedVal, node.subtree_best);
    }
  };

  for (const auto& node : nodes) {
    ASSERT_NO_FATAL_FAILURE(UpdateBest(node));
  }

  if (extra_node) {
    ASSERT_NO_FATAL_FAILURE(UpdateBest(*extra_node));
  }

  if (best.has_value()) {
    ASSERT_FALSE(tree.is_empty());
    ASSERT_EQ(best.value(), tree.root()->subtree_best);
  }

  for (auto iter = tree.begin(); iter != tree.end(); ++iter) {
    uint32_t expected_best = iter->augmented_val;
    if (iter.left()) {
      expected_best = std::min(expected_best, iter.left()->subtree_best);
    }
    if (iter.right()) {
      expected_best = std::min(expected_best, iter.right()->subtree_best);
    }

    ASSERT_EQ(expected_best, iter->subtree_best);
  }
}

TEST(WAVLTreeBestNodeObserverTests, BestInvariaintMaintained) {
  struct TestConfig {
    const uint64_t seed;
    const bool use_clear;
  };

  // Run the test a few different times with different random seeds, and at
  // least once where we clear the entire tree using |clear|, instead of
  // removing the elements one at a time.
  constexpr std::array kConfigs = {
      TestConfig{0x8a344d45e080e324, false},
      TestConfig{0xadbff1880c9ce89b, false},
      TestConfig{0x9a068f41344eec43, true},
  };

  for (const auto& cfg : kConfigs) {
    constexpr uint32_t kTestCount = 256;
    std::array<TestNode, kTestCount> test_nodes;
    std::array<uint32_t, kTestCount> shuffle_order;
    TestTree tree;

    // Initialize our array of TestNodes with unique primary keys, and random
    // augmented values.  Also initialize the shuffle order with a set of
    // ascending indices.
    std::mt19937_64 rng(cfg.seed);
    std::uniform_int_distribution<uint32_t> augmented_value_distribution(
        1, TestNode::kInvalidAugmentedVal - 1);

    for (uint32_t i = 0; i < kTestCount; ++i) {
      test_nodes[i].key = i;
      test_nodes[i].augmented_val = augmented_value_distribution(rng);
      shuffle_order[i] = i;
    }

    // Shuffle the order deck and add the test nodes to the tree in the shuffled
    // order, verifying the tree each time.
    ASSERT_NO_FATAL_FAILURE(ValidateTree(tree, test_nodes));
    std::shuffle(shuffle_order.begin(), shuffle_order.end(), rng);
    for (uint32_t ndx : shuffle_order) {
      tree.insert(&test_nodes[ndx]);
      ASSERT_NO_FATAL_FAILURE(ValidateTree(tree, test_nodes));
    }

    // Create a test node which is guaranteed to collide with test_nodes[0].
    // Also, give it an augmented value which is "better" then any of the values
    // in the tree.
    TestNode collision_node;
    collision_node.key = 0;
    collision_node.augmented_val = 0;

    // Attempt an insert-or-find operation using the collision node.  The insert
    // should fail, leaving the currently computed "best" value unchanged, but
    // if the Traits used included an, OnInsertCollision method, it should have
    // been called.
    typename TestTree::iterator already_in_tree;
    ASSERT_FALSE(tree.insert_or_find(&collision_node, &already_in_tree));
    ASSERT_EQ(&test_nodes[0], &*already_in_tree);
    ASSERT_NO_FATAL_FAILURE(ValidateTree(tree, test_nodes, &collision_node));

    // Now attempt an insert-or-replace using the collision node.  test_nodes[0]
    // should end up being replaced by collision_node, and
    // collision_node.augmented_val should become the new best of the tree.
    TestNode* replaced_node = tree.insert_or_replace(&collision_node);
    ASSERT_EQ(&test_nodes[0], replaced_node);
    ASSERT_NO_FATAL_FAILURE(ValidateTree(tree, test_nodes, &collision_node));

    // Depending on the test configuration, either simply clear the tree, or
    // shuffle the deck again and remove the nodes from the tree in the new
    // random order.
    if (cfg.use_clear) {
      tree.clear();
      ASSERT_NO_FATAL_FAILURE(ValidateTree(tree, test_nodes, &collision_node));
    } else {
      std::shuffle(shuffle_order.begin(), shuffle_order.end(), rng);
      for (uint32_t ndx : shuffle_order) {
        // Handle the fact that test_nodes[0] was replaced by collision_node
        if (ndx == 0) {
          tree.erase(collision_node);
        } else {
          tree.erase(test_nodes[ndx]);
        }
        ASSERT_NO_FATAL_FAILURE(ValidateTree(tree, test_nodes, &collision_node));
      }
    }
  }
}

}  // namespace
