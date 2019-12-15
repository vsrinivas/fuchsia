// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/transform_graph.h"

#include <array>

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

using flatland::TransformGraph;
using flatland::TransformHandle;

namespace {

static constexpr uint64_t kTreeRootIndex = 0;
static constexpr uint64_t kNumTreeTransforms = 7;
static constexpr uint64_t kLongIterationLength = 1000;

// This is a list of edges that form a filled binary tree three levels deep.
//
//       0
//     /   \
//    1     4
//   / \   / \
//  2   3 5   6
static constexpr std::pair<uint64_t, uint64_t> kTreeGraphEdges[] = {{0, 1}, {0, 4}, {1, 2},
                                                                    {1, 3}, {4, 5}, {4, 6}};

template <int size>
std::array<TransformHandle, size> CreateTransforms(TransformGraph& graph) {
  std::array<TransformHandle, size> transforms;

  for (auto& t : transforms) {
    t = graph.CreateTransform();
  }

  return transforms;
}

using TreeTransforms = std::array<TransformHandle, kNumTreeTransforms>;

TreeTransforms CreateTree(TransformGraph& graph) {
  TreeTransforms transforms = CreateTransforms<kNumTreeTransforms>(graph);

  for (auto edge : kTreeGraphEdges) {
    graph.AddChild(transforms[edge.first], transforms[edge.second]);
  }

  return transforms;
}

bool IsValidTopologicalSort(const TreeTransforms& transforms,
                            const TransformGraph::TopologyVector& vector) {
  static constexpr uint64_t kTreeParentIndices[] = {0, 0, 1, 1, 0, 4, 4};

  bool valid = true;

  valid &= vector.size() == kNumTreeTransforms;
  for (uint64_t i = 0; i < kNumTreeTransforms; ++i) {
    valid &= vector[i].handle == transforms[i];
    valid &= vector[i].parent_index == kTreeParentIndices[i];
  }

  return valid;
}

}  // namespace

namespace flatland {
namespace test {

TEST(TransformGraphTest, CreationAndDestruction) {
  TransformGraph graph;
  auto t1 = graph.CreateTransform();
  auto t2 = graph.CreateTransform();
  EXPECT_NE(t1, t2);
  EXPECT_TRUE(graph.ReleaseTransform(t1));
  // Releasing the same transform a second time should not succeed.
  EXPECT_FALSE(graph.ReleaseTransform(t1));
  EXPECT_TRUE(graph.ReleaseTransform(t2));
}

TEST(TransformGraphTest, ComputeAndCleanupOneTree) {
  TransformGraph graph;

  // Create a tree.
  auto tree = CreateTree(graph);

  // Topologically sort it and confirm that we get back a valid sorting.
  auto data = graph.ComputeAndCleanup(tree[kTreeRootIndex], kLongIterationLength);
  EXPECT_TRUE(IsValidTopologicalSort(tree, data.sorted_transforms));
  EXPECT_TRUE(data.dead_transforms.empty());
  EXPECT_TRUE(data.cyclical_edges.empty());

  // Release all children, keeping the top node alive, and re-confirm.
  for (uint64_t i = 1; i < kNumTreeTransforms; ++i) {
    EXPECT_TRUE(graph.ReleaseTransform(tree[i]));
  }
  data = graph.ComputeAndCleanup(tree[kTreeRootIndex], kLongIterationLength);
  EXPECT_TRUE(IsValidTopologicalSort(tree, data.sorted_transforms));
  EXPECT_TRUE(data.dead_transforms.empty());
  EXPECT_TRUE(data.cyclical_edges.empty());

  // Create a new node, release the root of the tree.
  auto new_root = graph.CreateTransform();
  graph.ReleaseTransform(tree[kTreeRootIndex]);

  // Confirm that all tree nodes appear in the dead transform list.
  data = graph.ComputeAndCleanup(new_root, kLongIterationLength);
  EXPECT_EQ(data.dead_transforms.size(), kNumTreeTransforms);
  for (auto transform : tree) {
    auto iter = data.dead_transforms.find(transform);
    EXPECT_NE(iter, data.dead_transforms.end());
    data.dead_transforms.erase(iter);
  }
}

TEST(TransformGraphTest, ComputeAndCleanupMultiTree) {
  TransformGraph graph;

  static constexpr uint64_t kNumTrees = 3;
  static constexpr uint64_t kErasedTree = 0;
  TreeTransforms trees[kNumTrees];

  // Create three trees, releasing all but the root nodes.
  for (auto& tree : trees) {
    tree = CreateTree(graph);
    for (uint64_t i = 1; i < kNumTreeTransforms; ++i) {
      graph.ReleaseTransform(tree[i]);
    }
  }

  // Confirm that all trees are valid.
  for (auto& tree : trees) {
    auto root = tree[kTreeRootIndex];
    auto data = graph.ComputeAndCleanup(root, kLongIterationLength);
    EXPECT_TRUE(IsValidTopologicalSort(tree, data.sorted_transforms));
  }

  // Release one of the trees.
  graph.ReleaseTransform(trees[kErasedTree][kTreeRootIndex]);

  // Confirm that all remaining trees are valid, and that the erased tree's transforms appear in the
  // dead transform list.
  for (uint64_t i = 1; i < kNumTrees; ++i) {
    auto root = trees[i][kTreeRootIndex];
    auto data = graph.ComputeAndCleanup(root, kLongIterationLength);
    EXPECT_TRUE(IsValidTopologicalSort(trees[i], data.sorted_transforms));
    if (i == 1) {
      EXPECT_EQ(data.dead_transforms.size(), kNumTreeTransforms);
      for (auto transform : trees[kErasedTree]) {
        auto iter = data.dead_transforms.find(transform);
        EXPECT_NE(iter, data.dead_transforms.end());
        data.dead_transforms.erase(iter);
      }
    }
  }
}

TEST(TransformGraphTest, ComputeAndCleanupMultiParent) {
  TransformGraph graph;

  static constexpr uint64_t kNumTransforms = 3;
  auto transforms = CreateTransforms<kNumTransforms>(graph);

  graph.AddChild(transforms[0], transforms[2]);
  graph.AddChild(transforms[1], transforms[2]);
  graph.ReleaseTransform(transforms[2]);

  // Transform 2 should be kept alive from child links, no matter where we traverse from.
  auto data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  EXPECT_EQ(data.dead_transforms.size(), 0u);
  data = graph.ComputeAndCleanup(transforms[1], kLongIterationLength);
  EXPECT_EQ(data.dead_transforms.size(), 0u);

  graph.RemoveChild(transforms[0], transforms[2]);

  // Transform 2 should still be alive, even if we ask for data rooted from the unlinked parent.
  data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  EXPECT_EQ(data.dead_transforms.size(), 0u);
  data = graph.ComputeAndCleanup(transforms[1], kLongIterationLength);
  EXPECT_EQ(data.dead_transforms.size(), 0u);

  graph.RemoveChild(transforms[1], transforms[2]);

  // Transform 2 should be cleaned up.
  data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  ASSERT_EQ(data.dead_transforms.size(), 1u);
  EXPECT_TRUE(data.dead_transforms.count(transforms[2]));
}

TEST(TransformGraphTest, CycleDetection) {
  TransformGraph graph;

  static constexpr uint64_t kNumTransforms = 5;
  static constexpr uint64_t kExpectedParentIndices[] = {0, 0, 1, 2, 3};

  auto transforms = CreateTransforms<kNumTransforms>(graph);

  for (uint64_t i = 0; i < kNumTransforms - 1; ++i) {
    graph.AddChild(transforms[i], transforms[i + 1]);
  }

  auto data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  for (uint64_t i = 0; i < kNumTransforms; ++i) {
    EXPECT_EQ(data.sorted_transforms[i].handle, transforms[i]);
    EXPECT_EQ(data.sorted_transforms[i].parent_index, kExpectedParentIndices[i]);
  }
  EXPECT_TRUE(data.cyclical_edges.empty());

  // Insert an indirect cycle.
  graph.AddChild(transforms[3], transforms[1]);

  data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  EXPECT_EQ(data.cyclical_edges.size(), 1u);
  auto iter = data.cyclical_edges.find(transforms[3]);
  EXPECT_NE(iter, data.cyclical_edges.end());
  EXPECT_EQ(iter->second, transforms[1]);

  // Insert a direct cycle.
  graph.AddChild(transforms[1], transforms[0]);
  data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  EXPECT_EQ(data.cyclical_edges.size(), 2u);

  // Cyclical edges includes the 3->1 edge
  iter = data.cyclical_edges.find(transforms[3]);
  EXPECT_NE(iter, data.cyclical_edges.end());
  EXPECT_EQ(iter->second, transforms[1]);

  // Cyclical edges includes the 1->0 edge
  iter = data.cyclical_edges.find(transforms[1]);
  EXPECT_NE(iter, data.cyclical_edges.end());
  EXPECT_EQ(iter->second, transforms[0]);
}

TEST(TransformGraphTest, ClearOperations) {
  TransformGraph graph;

  static constexpr uint64_t kNumTransforms = 3;
  auto transforms = CreateTransforms<kNumTransforms>(graph);

  // Adding children the first time is allowed.
  EXPECT_TRUE(graph.AddChild(transforms[0], transforms[1]));
  EXPECT_TRUE(graph.AddChild(transforms[0], transforms[2]));

  // Adding children the second time is invalid.
  EXPECT_FALSE(graph.AddChild(transforms[0], transforms[1]));
  EXPECT_FALSE(graph.AddChild(transforms[0], transforms[2]));

  // This test relies on previous topological tests for validity, and only checks that the length of
  // the returned vector is as expected.
  auto data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  EXPECT_EQ(data.sorted_transforms.size(), 3u);

  // Clearing the children only removes the child edges. All three handles are still valid.
  graph.ClearChildren(transforms[0]);
  data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  EXPECT_EQ(data.sorted_transforms.size(), 1u);
  EXPECT_TRUE(data.dead_transforms.empty());

  // Adding children after clearing is allowed.
  EXPECT_TRUE(graph.AddChild(transforms[0], transforms[1]));
  EXPECT_TRUE(graph.AddChild(transforms[0], transforms[2]));

  data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  EXPECT_EQ(data.sorted_transforms.size(), 3u);

  // The handle passed into ClearGraph is retained, but all of its state is removed.
  graph.ResetGraph(transforms[0]);
  data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  EXPECT_EQ(data.sorted_transforms.size(), 1u);
  EXPECT_EQ(data.dead_transforms.size(), 0u);

  // Old children no longer exist.
  EXPECT_FALSE(graph.RemoveChild(transforms[0], transforms[1]));
  EXPECT_FALSE(graph.RemoveChild(transforms[0], transforms[2]));

  // New children can be created.
  auto new_handle = graph.CreateTransform();
  EXPECT_TRUE(graph.AddChild(transforms[0], new_handle));
}

TEST(TransformGraphTest, IterationTestTooManyHandles) {
  TransformGraph graph;

  static constexpr uint64_t kNumTransforms = 10;
  static constexpr uint64_t kShortIterationLength = 5;

  auto transforms = CreateTransforms<kNumTransforms>(graph);

  auto good_data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  EXPECT_LE(good_data.iterations, kLongIterationLength);
  ASSERT_EQ(good_data.sorted_transforms.size(), 1u);
  EXPECT_EQ(good_data.sorted_transforms[0].handle, transforms[0]);
  EXPECT_EQ(good_data.dead_transforms.size(), 0u);

  auto bad_data = graph.ComputeAndCleanup(transforms[0], kShortIterationLength);
  EXPECT_GE(bad_data.iterations, kShortIterationLength);

  // The rest of this test shows that we can escape an 'invalid' graph by calling ResetGraph().
  graph.ResetGraph(transforms[0]);

  good_data = graph.ComputeAndCleanup(transforms[0], kShortIterationLength);
  // This is an indirect way to confirm that there is only a single transform in the working set.
  // One iteration to traverse transforms[0], one iteration because transforms[0] is in the working
  // set.
  EXPECT_EQ(good_data.iterations, 1u + 1u);
  EXPECT_EQ(good_data.sorted_transforms.size(), 1u);

  // This is an indirect way to confirm that transforms[0] and the new_transform are in the
  // working set.
  auto new_transform = graph.CreateTransform();
  EXPECT_TRUE(graph.AddChild(transforms[0], new_transform));
  EXPECT_TRUE(graph.AddChild(new_transform, transforms[0]));
}

TEST(TransformGraphTest, IterationTestTooManyPathsToChildren) {
  TransformGraph graph;

  static constexpr uint64_t kNumTransforms = 10;
  static constexpr uint64_t kChainDepth = 7;

  auto transforms = CreateTransforms<kNumTransforms>(graph);

  // Create a single-linked chain seven transforms deep.
  for (uint64_t i = 0; i < kChainDepth - 1; ++i) {
    graph.AddChild(transforms[i], transforms[i + 1]);
  }

  auto good_data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  // Transform graph should iterate over every transform in the working set (i.e., kNumTransforms),
  // as well as all of the children in the chain (i.e., kChainDepth).
  EXPECT_EQ(good_data.iterations, kNumTransforms + kChainDepth);
  EXPECT_EQ(good_data.sorted_transforms.size(), kChainDepth);
  EXPECT_EQ(good_data.dead_transforms.size(), 0u);

  // Connect all ten nodes together in three cascading diamonds.
  //
  //    0     visited 1 time
  //   / \
  //  1   7   visited 1 time
  //   \ /
  //    2     visited 2 times
  //   / \
  //  3   8   visited 2 times
  //   \ /
  //    4     visited 4 times
  //   / \
  //  5   9   visited 4 times
  //   \ /
  //    6     visited 8 times
  //
  // Total iterations = 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4 + 4 + 8 = 29
  static constexpr uint64_t kDiamondSize = 29u;

  graph.AddChild(transforms[0], transforms[7]);
  graph.AddChild(transforms[7], transforms[2]);
  graph.AddChild(transforms[2], transforms[8]);
  graph.AddChild(transforms[8], transforms[4]);
  graph.AddChild(transforms[4], transforms[9]);
  graph.AddChild(transforms[9], transforms[6]);

  for (uint64_t i = 1; i < kNumTransforms; ++i) {
    graph.ReleaseTransform(transforms[i]);
  }

  good_data = graph.ComputeAndCleanup(transforms[0], kLongIterationLength);
  // Transform graph should iterate over the diamond, plus one node in the working set (the root).
  EXPECT_EQ(good_data.iterations, kDiamondSize + 1u);
  EXPECT_EQ(good_data.sorted_transforms.size(), kDiamondSize);
  EXPECT_EQ(good_data.dead_transforms.size(), 0u);
}

TEST(TransformGraphTest, PriorityChildOrdering) {
  TransformGraph graph;

  // Create a normal child edge.
  auto parent = graph.CreateTransform();
  auto normal_child1 = graph.CreateTransform();
  graph.AddChild(parent, normal_child1);

  // Create a priority child edge.
  auto priority_child = graph.CreateTransform();
  graph.SetPriorityChild(parent, priority_child);

  // Create a second normal child edge.
  auto normal_child2 = graph.CreateTransform();
  graph.AddChild(parent, normal_child2);

  // Traverse the graph. The priority edge should come first, and the other two edges should be in
  // creation order.
  auto data = graph.ComputeAndCleanup(parent, kLongIterationLength);
  EXPECT_EQ(data.sorted_transforms.size(), 4u);
  EXPECT_EQ(data.sorted_transforms[0].handle, parent);
  EXPECT_EQ(data.sorted_transforms[1].handle, priority_child);
  EXPECT_EQ(data.sorted_transforms[2].handle, normal_child1);
  EXPECT_EQ(data.sorted_transforms[3].handle, normal_child2);

  // Remove the priority child.
  graph.ClearPriorityChild(parent);

  // Traverse the graph again. The priority child should no longer be present.
  data = graph.ComputeAndCleanup(parent, kLongIterationLength);

  EXPECT_EQ(data.sorted_transforms.size(), 3u);
  EXPECT_EQ(data.sorted_transforms[0].handle, parent);
  EXPECT_EQ(data.sorted_transforms[1].handle, normal_child1);
  EXPECT_EQ(data.sorted_transforms[2].handle, normal_child2);
}

TEST(TransformGraphTest, PriorityChildTrackedSeparately) {
  TransformGraph graph;

  // Create a normal child edge.
  auto parent = graph.CreateTransform();
  auto normal_child = graph.CreateTransform();
  graph.AddChild(parent, normal_child);

  // Create a priority child edge.
  auto priority_child = graph.CreateTransform();
  graph.SetPriorityChild(parent, priority_child);

  // Traverse the graph. The priority edge should come first, and the other two edges should be in
  // creation order.
  auto data = graph.ComputeAndCleanup(parent, kLongIterationLength);
  EXPECT_EQ(data.sorted_transforms.size(), 3u);
  EXPECT_EQ(data.sorted_transforms[0].handle, parent);
  EXPECT_EQ(data.sorted_transforms[1].handle, priority_child);
  EXPECT_EQ(data.sorted_transforms[2].handle, normal_child);

  // Clearing children from the parent shouldn't clear the priority child.
  graph.ClearChildren(parent);

  data = graph.ComputeAndCleanup(parent, kLongIterationLength);
  EXPECT_EQ(data.sorted_transforms.size(), 2u);
  EXPECT_EQ(data.sorted_transforms[0].handle, parent);
  EXPECT_EQ(data.sorted_transforms[1].handle, priority_child);

  // Nor should explicitly calling RemoveChild() on the priority child.
  bool result = graph.RemoveChild(parent, priority_child);
  EXPECT_FALSE(result);

  data = graph.ComputeAndCleanup(parent, kLongIterationLength);
  EXPECT_EQ(data.sorted_transforms.size(), 2u);
  EXPECT_EQ(data.sorted_transforms[0].handle, parent);
  EXPECT_EQ(data.sorted_transforms[1].handle, priority_child);
}

}  // namespace test
}  // namespace flatland
