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

}  // namespace view_tree::test
