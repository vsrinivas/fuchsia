// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/focus/focus_manager.h"

#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace focus::test {

enum : zx_koid_t { kNodeA = 1, kNodeB, kNodeC, kNodeD };

using view_tree::ViewNode;

namespace {

// Creates a snapshot with the following one-node topology:
//     A
std::shared_ptr<const view_tree::Snapshot> OneNodeSnapshot() {
  auto snapshot = std::make_shared<view_tree::Snapshot>();

  snapshot->root = kNodeA;
  auto& view_tree = snapshot->view_tree;
  view_tree[kNodeA] = ViewNode{.parent = ZX_KOID_INVALID};

  return snapshot;
}

// Creates a snapshot with the following two-node topology:
//     A
//     |
//     B
std::shared_ptr<const view_tree::Snapshot> TwoNodeSnapshot() {
  auto snapshot = std::make_shared<view_tree::Snapshot>();

  snapshot->root = kNodeA;
  auto& view_tree = snapshot->view_tree;
  view_tree[kNodeA] = ViewNode{.parent = ZX_KOID_INVALID, .children = {kNodeA}};
  view_tree[kNodeB] = ViewNode{.parent = kNodeA};

  return snapshot;
}

// Creates a snapshot with the following three-node topology:
//     A
//     |
//     B
//     |
//     C
std::shared_ptr<const view_tree::Snapshot> ThreeNodeSnapshot() {
  auto snapshot = std::make_shared<view_tree::Snapshot>();

  snapshot->root = kNodeA;
  auto& view_tree = snapshot->view_tree;
  view_tree[kNodeA] = ViewNode{.parent = ZX_KOID_INVALID, .children = {kNodeA}};
  view_tree[kNodeB] = ViewNode{.parent = kNodeA, .children = {kNodeC}};
  view_tree[kNodeC] = ViewNode{.parent = kNodeB};

  return snapshot;
}

// Creates a snapshot with the following four-node topology:
//      A
//    /   \
//   B     C
//   |
//   D
std::shared_ptr<const view_tree::Snapshot> FourNodeSnapshot() {
  auto snapshot = std::make_shared<view_tree::Snapshot>();

  snapshot->root = kNodeA;
  auto& view_tree = snapshot->view_tree;
  view_tree[kNodeA] = ViewNode{.parent = ZX_KOID_INVALID, .children = {kNodeB, kNodeC}};
  view_tree[kNodeB] = ViewNode{.parent = kNodeA, .children = {kNodeD}};
  view_tree[kNodeC] = ViewNode{.parent = kNodeA};
  view_tree[kNodeD] = ViewNode{.parent = kNodeB};

  return snapshot;
}

}  // namespace

TEST(FocusManagerTest, EmptyTransitions) {
  FocusManager focus_manager;

  EXPECT_TRUE(focus_manager.focus_chain().empty());

  // Empty snapshot should not affect the empty focus chain.
  focus_manager.OnNewViewTreeSnapshot(std::make_shared<view_tree::Snapshot>());
  EXPECT_TRUE(focus_manager.focus_chain().empty());

  // A non-empty snapshot should affect the focus chain.
  focus_manager.OnNewViewTreeSnapshot(OneNodeSnapshot());
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA));

  // Submitting the same snapshot again should not change the focus chain.
  focus_manager.OnNewViewTreeSnapshot(OneNodeSnapshot());
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA));
}

// Tree topology:
//     A
//     |
//     B
TEST(FocusManagerTest, FocusTransferDownAllowed) {
  FocusManager focus_manager;
  focus_manager.OnNewViewTreeSnapshot(TwoNodeSnapshot());

  EXPECT_EQ(focus_manager.RequestFocus(kNodeA, kNodeB), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB));
}

// Tree topology:
//     A
//     |
//     B
TEST(FocusManagerTest, FocusTransferToSameNode_ShouldHaveNoEffect) {
  FocusManager focus_manager;
  focus_manager.OnNewViewTreeSnapshot(TwoNodeSnapshot());

  EXPECT_EQ(focus_manager.RequestFocus(kNodeA, kNodeB), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB));

  EXPECT_EQ(focus_manager.RequestFocus(kNodeA, kNodeB), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB));
}

// Tree topology:
//     A
//     |
//     B
TEST(FocusManagerTest, FocusTransferToSelfAllowed) {
  FocusManager focus_manager;
  focus_manager.OnNewViewTreeSnapshot(TwoNodeSnapshot());

  // Transfer focus to B.
  EXPECT_EQ(focus_manager.RequestFocus(kNodeA, kNodeB), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB));

  // Transfer focus back to A, on the authority of A.
  EXPECT_EQ(focus_manager.RequestFocus(kNodeA, kNodeA), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA));
}

// Tree topology:
//     A
//     |
//     B
TEST(FocusManagerTest, FocusTransferUpwardDenied) {
  FocusManager focus_manager;
  focus_manager.OnNewViewTreeSnapshot(TwoNodeSnapshot());

  // Transfer focus to B.
  EXPECT_EQ(focus_manager.RequestFocus(kNodeA, kNodeB), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB));

  // Requesting change to A from B should fail and no change should be observed on the focus chain.
  EXPECT_EQ(focus_manager.RequestFocus(kNodeB, kNodeA),
            FocusChangeStatus::kErrorRequestorNotRequestAncestor);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB));
}

// Tree topology:
//         A
//      /    \
//     B      C
//     |
//     D
TEST(FocusManagerTest, BranchedTree) {
  FocusManager focus_manager;
  focus_manager.OnNewViewTreeSnapshot(FourNodeSnapshot());

  // Transfer focus from A to C.
  EXPECT_EQ(focus_manager.RequestFocus(kNodeA, kNodeC), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeC));

  // Transfer focus from A to D.
  EXPECT_EQ(focus_manager.RequestFocus(kNodeA, kNodeD), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB, kNodeD));

  // Transfer focus from A to B.
  EXPECT_EQ(focus_manager.RequestFocus(kNodeA, kNodeB), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB));

  // Transfer focus from B to D.
  EXPECT_EQ(focus_manager.RequestFocus(kNodeB, kNodeD), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB, kNodeD));
}

// Tree topology:
//         A
//      /    \
//     B      C
//     |
//     D
TEST(FocusManagerTest, SiblingTransferRequestsDenied) {
  FocusManager focus_manager;
  focus_manager.OnNewViewTreeSnapshot(FourNodeSnapshot());

  // Setup: Transfer to "D".
  EXPECT_EQ(focus_manager.RequestFocus(kNodeA, kNodeD), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB, kNodeD));

  // Transfer request from "B" to "C" denied.
  EXPECT_EQ(focus_manager.RequestFocus(kNodeB, kNodeC),
            FocusChangeStatus::kErrorRequestorNotRequestAncestor);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB, kNodeD));

  // Transfer request from "D" to "C" denied.
  EXPECT_EQ(focus_manager.RequestFocus(kNodeD, kNodeC),
            FocusChangeStatus::kErrorRequestorNotRequestAncestor);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB, kNodeD));
}

// Tree topology:
//     A      A     A
//     |      |
//     B  ->  B  ->    ->
//     |
//     C
TEST(FocusManagerTest, ViewRemoval_ShouldShortenFocusChain) {
  FocusManager focus_manager;
  focus_manager.OnNewViewTreeSnapshot(ThreeNodeSnapshot());

  // Emulate a focus transfer from "A" to "C".
  EXPECT_EQ(focus_manager.RequestFocus(kNodeA, kNodeC), FocusChangeStatus::kAccept);
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB, kNodeC));

  // Client "C" destroys its view.
  focus_manager.OnNewViewTreeSnapshot(TwoNodeSnapshot());
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA, kNodeB));

  // Client "B" destroys its view.
  focus_manager.OnNewViewTreeSnapshot(OneNodeSnapshot());
  EXPECT_THAT(focus_manager.focus_chain(), testing::ElementsAre(kNodeA));

  focus_manager.OnNewViewTreeSnapshot(std::make_shared<view_tree::Snapshot>());
  EXPECT_TRUE(focus_manager.focus_chain().empty());
}

}  // namespace focus::test
