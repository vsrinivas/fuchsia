// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/view_tree_snapshotter.h"

#include <lib/async-testing/test_loop.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace view_tree::test {

namespace {

enum : zx_koid_t {
  kRoot1A = 1,
  kNode2,
  kNode3,
  kRoot4B,
  kNode5,
  kRoot6C,
  kNode7,
  kNode8,
  kNode9,
  kNode10,
  kNode11,
};

// Generates a valid tree out of three subtrees: A, B and C
//  ViewTrees:           Unconnected nodes:
// -------------         -----------------------
// | A   1     |         | A 8 | B 9 | C 10 11 |
// |   /   \   |         -----------------------
// |  2     3  |
// |  |     |  |
// -------------
// |B 4  |C 6  |
// |  |  |  |  |
// |  5  |  7  |
// ------ -----
std::vector<SubtreeSnapshotGenerator> BasicTree() {
  std::vector<SubtreeSnapshotGenerator> subtree_generators;

  // A
  subtree_generators.emplace_back([] {
    SubtreeSnapshot subtree;
    auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = subtree;

    root = kRoot1A;
    view_tree[kRoot1A] = ViewNode{.parent = ZX_KOID_INVALID, .children = {kNode2, kNode3}};
    view_tree[kNode2] = ViewNode{.parent = kRoot1A, .children = {kRoot4B}};
    view_tree[kNode3] = ViewNode{.parent = kRoot1A, .children = {kRoot6C}};
    unconnected_views = {kNode8};

    tree_boundaries.emplace(kNode2, kRoot4B);
    tree_boundaries.emplace(kNode3, kRoot6C);
    return subtree;
  });

  // B
  subtree_generators.emplace_back([] {
    SubtreeSnapshot subtree;
    auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = subtree;

    root = kRoot4B;
    view_tree[kRoot4B] = ViewNode{.parent = ZX_KOID_INVALID, .children = {kNode5}};
    view_tree[kNode5] = ViewNode{.parent = kRoot4B, .children = {}};
    unconnected_views = {kNode9};
    return subtree;
  });

  // C
  subtree_generators.emplace_back([] {
    SubtreeSnapshot subtree;
    auto& [root, view_tree, unconnected_views, hit_tester, tree_boundaries] = subtree;

    root = kRoot6C;
    view_tree[kRoot6C] = ViewNode{.parent = ZX_KOID_INVALID, .children = {kNode7}};
    view_tree[kNode7] = ViewNode{.parent = kRoot6C, .children = {}};
    unconnected_views = {kNode10, kNode11};
    return subtree;
  });

  return subtree_generators;
}

// Expected combined Snapshot from BasicTree() above.
Snapshot BasicTreeSnapshot() {
  Snapshot snapshot;
  snapshot.root = kRoot1A;

  {
    auto& view_tree = snapshot.view_tree;
    view_tree[kRoot1A] = ViewNode{.parent = ZX_KOID_INVALID, .children = {kNode2, kNode3}};
    view_tree[kNode2] = ViewNode{.parent = kRoot1A, .children = {kRoot4B}};
    view_tree[kNode3] = ViewNode{.parent = kRoot1A, .children = {kRoot6C}};
    view_tree[kRoot4B] = ViewNode{.parent = kNode2, .children = {kNode5}};
    view_tree[kNode5] = ViewNode{.parent = kRoot4B};
    view_tree[kRoot6C] = ViewNode{.parent = kNode3, .children = {kNode7}};
    view_tree[kNode7] = ViewNode{.parent = kRoot6C};
  }

  snapshot.unconnected_views = {kNode8, kNode9, kNode10, kNode11};

  return snapshot;
}

}  // namespace

// Checks that BasicTree() gets combined to the correct Snapshot, and that the snapshot is
// correctly delivered to a subscriber.
TEST(ViewTreeSnapshotterTest, BasicTreeTest) {
  std::vector<ViewTreeSnapshotter::Subscriber> subscribers;
  async::TestLoop loop;
  bool callback_fired = false;
  subscribers.push_back({.on_new_view_tree =
                             [&callback_fired](std::shared_ptr<const Snapshot> snapshot) {
                               callback_fired = true;
                               const bool conversion_correct = *snapshot == BasicTreeSnapshot();
                               EXPECT_TRUE(conversion_correct);
                               if (!conversion_correct) {
                                 FX_LOGS(ERROR) << "Generated snapshot:\n"
                                                << (*snapshot) << "\ndid not match expected:\n\n"
                                                << BasicTreeSnapshot();
                               }
                             },
                         .dispatcher = loop.dispatcher()});

  ViewTreeSnapshotter tree(BasicTree(), std::move(subscribers));

  tree.UpdateSessions({}, {});
  loop.RunUntilIdle();
  EXPECT_TRUE(callback_fired);
}

// Check that the subscriber fires on the supplied dispatcher and doesn't rely on the default
// dispatcher.
TEST(ViewTreeSnapshotterTest, Subscriber_RunsOnCorrectDispatcher) {
  std::vector<ViewTreeSnapshotter::Subscriber> subscribers;
  async::TestLoop loop1;
  async::TestLoop loop2;
  async_set_default_dispatcher(loop1.dispatcher());
  bool callback_fired = false;
  subscribers.push_back({.on_new_view_tree = [&callback_fired](auto) { callback_fired = true; },
                         .dispatcher = loop2.dispatcher()});

  ViewTreeSnapshotter tree(BasicTree(), std::move(subscribers));

  tree.UpdateSessions({}, {});

  EXPECT_FALSE(callback_fired);
  loop1.RunUntilIdle();
  EXPECT_FALSE(callback_fired);
  loop2.RunUntilIdle();
  EXPECT_TRUE(callback_fired);
}

TEST(ViewTreeSnapshotterTest, MultipleSubscribers) {
  std::vector<ViewTreeSnapshotter::Subscriber> subscribers;

  async::TestLoop loop;
  std::shared_ptr<const Snapshot> snapshot1;
  subscribers.push_back({.on_new_view_tree = [&snapshot1](auto snapshot) { snapshot1 = snapshot; },
                         .dispatcher = loop.dispatcher()});
  std::shared_ptr<const Snapshot> snapshot2;
  subscribers.push_back({.on_new_view_tree = [&snapshot2](auto snapshot) { snapshot2 = snapshot; },
                         .dispatcher = loop.dispatcher()});
  async::TestLoop loop2;
  std::shared_ptr<const Snapshot> snapshot3;
  subscribers.push_back({.on_new_view_tree = [&snapshot3](auto snapshot) { snapshot3 = snapshot; },
                         .dispatcher = loop2.dispatcher()});

  ViewTreeSnapshotter tree(BasicTree(), std::move(subscribers));

  tree.UpdateSessions({}, {});
  loop.RunUntilIdle();
  EXPECT_TRUE(snapshot1);
  EXPECT_TRUE(snapshot2);
  EXPECT_FALSE(snapshot3);
  loop2.RunUntilIdle();
  EXPECT_TRUE(snapshot3);

  // Should all be pointing to the same snapshot.
  EXPECT_EQ(snapshot1, snapshot2);
  EXPECT_EQ(snapshot1, snapshot3);
}

// Check that multiple calls to UpdateSessions() are handled correctly.
TEST(ViewTreeSnapshotterTest, MultipleUpdateSessionsCalls) {
  std::vector<SubtreeSnapshotGenerator> subtrees;
  bool first_call = true;
  subtrees.emplace_back([&first_call] {
    SubtreeSnapshot subtree;
    if (first_call) {
      subtree.root = kRoot1A;
      subtree.view_tree[kRoot1A] = ViewNode{};
    } else {
      subtree.root = kRoot4B;
      subtree.view_tree[kRoot4B] = ViewNode{};
    }
    first_call = false;
    return subtree;
  });

  std::vector<ViewTreeSnapshotter::Subscriber> subscribers;
  async::TestLoop loop;
  std::shared_ptr<const Snapshot> snapshot1;
  subscribers.push_back({.on_new_view_tree = [&snapshot1](auto snapshot) { snapshot1 = snapshot; },
                         .dispatcher = loop.dispatcher()});

  ViewTreeSnapshotter tree(std::move(subtrees), std::move(subscribers));

  tree.UpdateSessions({}, {});
  loop.RunUntilIdle();
  ASSERT_TRUE(snapshot1);
  EXPECT_EQ(snapshot1->root, kRoot1A);

  std::shared_ptr<const Snapshot> snapshot1_copy = snapshot1;
  EXPECT_EQ(snapshot1_copy, snapshot1);

  tree.UpdateSessions({}, {});
  loop.RunUntilIdle();
  EXPECT_NE(snapshot1_copy, snapshot1);
  EXPECT_EQ(snapshot1->root, kRoot4B);
}

// Test that a callback queued on a subscriber thread survives the death of ViewTreeSnapshotter.
TEST(ViewTreeSnapshotterTest, SubscriberCallbackLifetime) {
  std::vector<SubtreeSnapshotGenerator> subtrees;
  subtrees.emplace_back([] {
    SubtreeSnapshot subtree;
    subtree.root = kRoot1A;
    subtree.view_tree[kRoot1A] = ViewNode{};
    return subtree;
  });

  std::vector<ViewTreeSnapshotter::Subscriber> subscribers;
  async::TestLoop loop;
  std::shared_ptr<const Snapshot> snapshot1;
  int called_count = 0;
  subscribers.push_back({.on_new_view_tree =
                             [&snapshot1, &called_count](auto snapshot) {
                               snapshot1 = snapshot;
                               ++called_count;
                             },
                         .dispatcher = loop.dispatcher()});

  auto tree = std::make_unique<ViewTreeSnapshotter>(std::move(subtrees), std::move(subscribers));

  tree->UpdateSessions({}, {});
  tree->UpdateSessions({}, {});
  tree.reset();
  EXPECT_EQ(called_count, 0);

  loop.RunUntilIdle();
  EXPECT_EQ(called_count, 2);
  ASSERT_TRUE(snapshot1);
  EXPECT_EQ(snapshot1->root, kRoot1A);
}

}  // namespace view_tree::test
