// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

#include <lib/async-testing/test_loop.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace view_tree::test {

// Test that we early-out for ndoes not in the ViewTree.
TEST(SnapshotHitTestTest, NodeNotInViewTree) {
  Snapshot snapshot;
  snapshot.hit_testers.push_back(
      [](zx_koid_t koid, glm::vec2, bool) { return SubtreeHitTestResult{.hits = {1}}; });

  // Node not in the view tree should return empty.
  std::vector<zx_koid_t> hits =
      snapshot.HitTest(/*koid*/ 49, glm::vec2(0.f), /*is_semantic*/ false);
  EXPECT_TRUE(hits.empty());
}

TEST(SnapshotHitTestTest, MultipleSubtrees) {
  Snapshot snapshot;
  snapshot.hit_testers.push_back([](zx_koid_t koid, glm::vec2, bool) {
    if (koid == 1) {
      return SubtreeHitTestResult{.hits = {1, 2, 3},
                                  .continuations = {
                                      // Should be inserted before index 0, i.e. at the start
                                      {/*index*/ 0, /*koid*/ 4},
                                      // Should be inserted before index 3, i.e. at the end.
                                      {/*index*/ 3, /*koid*/ 5},
                                  }};
    } else {
      return SubtreeHitTestResult{};
    }
  });
  snapshot.hit_testers.push_back([](zx_koid_t koid, glm::vec2, bool) {
    if (koid == 4) {
      return SubtreeHitTestResult{.hits = {6, 7, 8}};
    } else {
      return SubtreeHitTestResult{};
    }
  });
  snapshot.hit_testers.push_back([](zx_koid_t koid, glm::vec2, bool) {
    if (koid == 5) {
      return SubtreeHitTestResult{.hits = {9, 10}};
    } else {
      return SubtreeHitTestResult{};
    }
  });

  // Add the starting nodes to the ViewTree,
  snapshot.view_tree[1];
  snapshot.view_tree[4];
  snapshot.view_tree[5];

  // Test subtrees individually.
  {
    std::vector<zx_koid_t> hits =
        snapshot.HitTest(/*koid*/ 4, glm::vec2(0.f), /*is_semantic*/ false);
    EXPECT_THAT(hits, testing::ElementsAre(6, 7, 8));
  }
  {
    std::vector<zx_koid_t> hits =
        snapshot.HitTest(/*koid*/ 5, glm::vec2(0.f), /*is_semantic*/ false);
    EXPECT_THAT(hits, testing::ElementsAre(9, 10));
  }

  // Check that continuations are correctly flattened.
  {
    std::vector<zx_koid_t> hits =
        snapshot.HitTest(/*koid*/ 1, glm::vec2(0.f), /*is_semantic*/ false);
    EXPECT_THAT(hits, testing::ElementsAre(6, 7, 8, 1, 2, 3, 9, 10));
  }
}

TEST(SnapshotHitTestTest, ContinuationsShouldHonorInsertionOrder) {
  Snapshot snapshot;

  // Two hit tests without continuations.
  snapshot.hit_testers.push_back([](zx_koid_t koid, glm::vec2, bool) {
    if (koid == 4) {
      return SubtreeHitTestResult{.hits = {6, 7, 8}};
    } else {
      return SubtreeHitTestResult{};
    }
  });
  snapshot.hit_testers.push_back([](zx_koid_t koid, glm::vec2, bool) {
    if (koid == 5) {
      return SubtreeHitTestResult{.hits = {9, 10}};
    } else {
      return SubtreeHitTestResult{};
    }
  });

  // Two subtrees with the same continuations in opposite order.
  snapshot.hit_testers.push_back([](zx_koid_t koid, glm::vec2, bool) {
    if (koid == 100) {
      return SubtreeHitTestResult{
          .hits = {1, 2, 3},
          .continuations = {
              // Two continuations at same index. Insertion order should matter.
              {/*index*/ 1, /*koid*/ 4},
              {/*index*/ 1, /*koid*/ 5},
          }};
    } else {
      return SubtreeHitTestResult{};
    }
  });
  snapshot.hit_testers.push_back([](zx_koid_t koid, glm::vec2, bool) {
    if (koid == 101) {
      return SubtreeHitTestResult{.hits = {1, 2, 3},
                                  .continuations = {
                                      // Both inserted at same index. Insertion order should matter.
                                      {/*index*/ 1, /*koid*/ 5},
                                      {/*index*/ 1, /*koid*/ 4},
                                  }};
    } else {
      return SubtreeHitTestResult{};
    }
  });

  // Add the starting nodes to the ViewTree.
  snapshot.view_tree[4];
  snapshot.view_tree[5];
  snapshot.view_tree[100];
  snapshot.view_tree[101];

  // Check that continuations honor insertion order for index ties.
  {
    std::vector<zx_koid_t> hits =
        snapshot.HitTest(/*koid*/ 100, glm::vec2(0.f), /*is_semantic*/ false);
    EXPECT_THAT(hits, testing::ElementsAre(1, 6, 7, 8, 9, 10, 2, 3));
  }
  {
    std::vector<zx_koid_t> hits =
        snapshot.HitTest(/*koid*/ 101, glm::vec2(0.f), /*is_semantic*/ false);
    EXPECT_THAT(hits, testing::ElementsAre(1, 9, 10, 6, 7, 8, 2, 3));
  }
}

// Check IsDescendant() for various combinations in this ViewTree:
//    1
//  /   \
// 2     3
// |     |
// 4     5
TEST(IsDescendantTest, Comprehensive) {
  Snapshot snapshot;
  {
    auto& view_tree = snapshot.view_tree;
    view_tree[1].parent = ZX_KOID_INVALID;
    view_tree[1].children = {2, 3};
    view_tree[2].parent = 1;
    view_tree[2].children = {4};
    view_tree[3].parent = 1;
    view_tree[3].children = {5};
    view_tree[4].parent = 2;
    view_tree[5].parent = 3;
  }

  // Check all the valid parent chains.
  EXPECT_TRUE(snapshot.IsDescendant(/*descendant*/ 2, /*ancestor*/ 1));
  EXPECT_TRUE(snapshot.IsDescendant(/*descendant*/ 3, /*ancestor*/ 1));
  EXPECT_TRUE(snapshot.IsDescendant(/*descendant*/ 4, /*ancestor*/ 1));
  EXPECT_TRUE(snapshot.IsDescendant(/*descendant*/ 5, /*ancestor*/ 1));
  EXPECT_TRUE(snapshot.IsDescendant(/*descendant*/ 4, /*ancestor*/ 2));
  EXPECT_TRUE(snapshot.IsDescendant(/*descendant*/ 5, /*ancestor*/ 3));

  // Check some invalid ones.
  EXPECT_FALSE(snapshot.IsDescendant(/*descendant*/ 1, /*ancestor*/ 2));
  EXPECT_FALSE(snapshot.IsDescendant(/*descendant*/ 1, /*ancestor*/ 4));
  EXPECT_FALSE(snapshot.IsDescendant(/*descendant*/ 2, /*ancestor*/ 4));
  EXPECT_FALSE(snapshot.IsDescendant(/*descendant*/ 1, /*ancestor*/ 1));
  EXPECT_FALSE(snapshot.IsDescendant(/*descendant*/ 5, /*ancestor*/ 2));
  EXPECT_FALSE(snapshot.IsDescendant(/*descendant*/ 2, /*ancestor*/ 3));
  EXPECT_FALSE(snapshot.IsDescendant(/*descendant*/ 2, /*ancestor*/ ZX_KOID_INVALID));
  EXPECT_FALSE(snapshot.IsDescendant(/*descendant*/ 124124, /*ancestor*/ 1));
}

// Check GetAncestorsOf() for various nodes in this ViewTree:
//    1
//  /   \
// 2     3
// |     |
// 4     5
TEST(GetAncestorsOfTest, Comprehensive) {
  Snapshot snapshot;
  {
    auto& view_tree = snapshot.view_tree;
    view_tree[1].parent = ZX_KOID_INVALID;
    view_tree[1].children = {2, 3};
    view_tree[2].parent = 1;
    view_tree[2].children = {4};
    view_tree[3].parent = 1;
    view_tree[3].children = {5};
    view_tree[4].parent = 2;
    view_tree[5].parent = 3;
  }

  // Check all the valid parent chains.
  EXPECT_TRUE(snapshot.GetAncestorsOf(1).empty());
  EXPECT_THAT(snapshot.GetAncestorsOf(2), testing::ElementsAre(1));
  EXPECT_THAT(snapshot.GetAncestorsOf(3), testing::ElementsAre(1));
  EXPECT_THAT(snapshot.GetAncestorsOf(4), testing::ElementsAre(2, 1));
  EXPECT_THAT(snapshot.GetAncestorsOf(5), testing::ElementsAre(3, 1));
}

// Check if the == operator works correctly.
TEST(ViewNodeComparisonTest, Comprehensive) {
  BoundingBox box1 = {.min = {1.0, 2.0}, .max = {4.0, 5.0}};
  BoundingBox box2 = {.min = {1.0, 2.0}, .max = {4.0, 5.0}};
  glm::mat4 transform1 = {1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4};
  glm::mat4 transform2 = {1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4};
  fuchsia::ui::views::ViewRef view_ref;
  auto view_ref_1 = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_ref));
  auto view_ref_2 = view_ref_1;
  // Equality operator should work correctly when two nodes are equal.
  {
    ViewNode view_node_1 = {.parent = 1,
                            .children = {},
                            .bounding_box = std::move(box1),
                            .local_from_world_transform = std::move(transform1),
                            .is_focusable = true,
                            .view_ref = view_ref_1,
                            .debug_name = "view_node"};
    ViewNode view_node_2 = {.parent = 1,
                            .children = {},
                            .bounding_box = std::move(box2),
                            .local_from_world_transform = std::move(transform2),
                            .is_focusable = true,
                            .view_ref = view_ref_2,
                            .debug_name = "view_node"};
    EXPECT_EQ(view_node_1, view_node_2);
  }
  // Equality operator should work correctly when two nodes do not have the same debug name.
  {
    ViewNode view_node_1 = {.parent = 1,
                            .children = {},
                            .bounding_box = std::move(box1),
                            .local_from_world_transform = std::move(transform1),
                            .is_focusable = true,
                            .view_ref = view_ref_1,
                            .debug_name = "view_node_1"};
    ViewNode view_node_2 = {.parent = 1,
                            .children = {},
                            .bounding_box = std::move(box2),
                            .local_from_world_transform = std::move(transform2),
                            .is_focusable = true,
                            .view_ref = view_ref_2,
                            .debug_name = "view_node_2"};
    EXPECT_FALSE(view_node_1 == view_node_2);
  }
}

}  // namespace view_tree::test
