// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <optional>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/focus/focus_manager.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

// This test exercises the implementation of the fuchsia.ui.views.ViewRefFocused protocol, which
// allows clients to listen to view-focus events.
//
// Visual geometry is not important in this test. We use the following two-node tree topology:
//   A
//   |
//   B
namespace {

enum : zx_koid_t { kNodeA = 1, kNodeB };

// Creates a snapshot with the following two-node topology:
//     A
//     |
//     B
std::shared_ptr<const view_tree::Snapshot> TwoNodeSnapshot() {
  auto snapshot = std::make_shared<view_tree::Snapshot>();

  snapshot->root = kNodeA;
  auto& view_tree = snapshot->view_tree;
  view_tree[kNodeA] = view_tree::ViewNode{.parent = ZX_KOID_INVALID, .children = {kNodeB}};
  view_tree[kNodeB] = view_tree::ViewNode{.parent = kNodeA};

  return snapshot;
}

// Class fixture for TEST_F.
class ViewRefFocusedTest : public gtest::TestLoopFixture {
 protected:
  ViewRefFocusedTest() {
    focus_manager_.RegisterViewRefFocused(kNodeA, node_a_focused_.NewRequest());
    focus_manager_.RegisterViewRefFocused(kNodeB, node_b_focused_.NewRequest());

    FX_CHECK(node_a_focused_.is_bound());
    FX_CHECK(node_b_focused_.is_bound());
  }

  focus::FocusManager focus_manager_;
  fuchsia::ui::views::ViewRefFocusedPtr node_a_focused_;
  fuchsia::ui::views::ViewRefFocusedPtr node_b_focused_;
};

TEST_F(ViewRefFocusedTest, NoFocus_NoResponse) {
  // No snapshots declared yet, "empty scene".

  bool called_node_a = false;
  node_a_focused_->Watch([&called_node_a](auto) { called_node_a = true; });

  bool called_node_b = false;
  node_b_focused_->Watch([&called_node_b](auto) { called_node_b = true; });

  RunLoopUntilIdle();
  EXPECT_FALSE(called_node_a);
  EXPECT_FALSE(called_node_b);
}

TEST_F(ViewRefFocusedTest, BasicTree_ParentGetsFocus) {
	focus_manager_.OnNewViewTreeSnapshot(TwoNodeSnapshot());

  std::optional<bool> node_a_focus;
  node_a_focused_->Watch([&node_a_focus](auto update) {
    ASSERT_TRUE(update.has_focused());
    node_a_focus = std::optional<bool>(update.focused());
  });

  bool called_node_b = false;
  node_b_focused_->Watch([&called_node_b](auto) { called_node_b = true; });

  RunLoopUntilIdle();
  ASSERT_TRUE(node_a_focus.has_value());  // received a focus event
  EXPECT_TRUE(node_a_focus.value());      // node A has focus
  EXPECT_FALSE(called_node_b);
}

TEST_F(ViewRefFocusedTest, ChildFocus_FalseToTrue) {
	focus_manager_.OnNewViewTreeSnapshot(TwoNodeSnapshot());

  // Poll after node B gains focus.
  std::optional<bool> node_b_focus;
  node_b_focused_->Watch([&node_b_focus](auto update) {
    ASSERT_TRUE(update.has_focused());
    node_b_focus = std::optional<bool>(update.focused());
  });

  RunLoopUntilIdle();
  EXPECT_FALSE(node_b_focus.has_value());

  focus_manager_.RequestFocus(kNodeA, kNodeB);

  RunLoopUntilIdle();
  ASSERT_TRUE(node_b_focus.has_value());
  EXPECT_TRUE(node_b_focus.value());
}

TEST_F(ViewRefFocusedTest, ChildFocus_FalseToFalse) {
	focus_manager_.OnNewViewTreeSnapshot(TwoNodeSnapshot());
  focus_manager_.RequestFocus(kNodeA, kNodeB);
  focus_manager_.RequestFocus(kNodeA, kNodeA);

  // Poll after node B gains then loses focus.
  std::optional<bool> node_b_focus;
  node_b_focused_->Watch([&node_b_focus](auto update) {
    ASSERT_TRUE(update.has_focused());
    node_b_focus = std::optional<bool>(update.focused());
  });

  RunLoopUntilIdle();
  ASSERT_TRUE(node_b_focus.has_value());
  EXPECT_FALSE(node_b_focus.value());
}

TEST_F(ViewRefFocusedTest, ChildFocus_TrueToFalse) {
	focus_manager_.OnNewViewTreeSnapshot(TwoNodeSnapshot());
  focus_manager_.RequestFocus(kNodeA, kNodeB);

  // First poll by node B sees focus gained.
  std::optional<bool> node_b_focus;
  node_b_focused_->Watch([&node_b_focus](auto update) {
    ASSERT_TRUE(update.has_focused());
    node_b_focus = std::optional<bool>(update.focused());
  });

  RunLoopUntilIdle();
  ASSERT_TRUE(node_b_focus.has_value());
  EXPECT_TRUE(node_b_focus.value());

  // Second poll by node B sees focus lost.
  node_b_focus.reset();
  node_b_focused_->Watch([&node_b_focus](auto update) {
    ASSERT_TRUE(update.has_focused());
    node_b_focus = std::optional<bool>(update.focused());
  });

  focus_manager_.RequestFocus(kNodeA, kNodeA);

  RunLoopUntilIdle();
  ASSERT_TRUE(node_b_focus.has_value());
  EXPECT_FALSE(node_b_focus.value());
}

TEST_F(ViewRefFocusedTest, ChildFocus_TrueToTrue) {
	focus_manager_.OnNewViewTreeSnapshot(TwoNodeSnapshot());
  focus_manager_.RequestFocus(kNodeA, kNodeB);

  // First poll by node B sees focus gained.
  std::optional<bool> node_b_focus;
  node_b_focused_->Watch([&node_b_focus](auto update) {
    ASSERT_TRUE(update.has_focused());
    node_b_focus = std::optional<bool>(update.focused());
  });

  RunLoopUntilIdle();
  ASSERT_TRUE(node_b_focus.has_value());
  EXPECT_TRUE(node_b_focus.value());

  // Second poll by node B sees focus lost then gained.
  node_b_focus.reset();
  node_b_focused_->Watch([&node_b_focus](auto update) {
    ASSERT_TRUE(update.has_focused());
    node_b_focus = std::optional<bool>(update.focused());
  });

  focus_manager_.RequestFocus(kNodeA, kNodeA);
  focus_manager_.RequestFocus(kNodeA, kNodeB);

  RunLoopUntilIdle();
  ASSERT_TRUE(node_b_focus.has_value());
  EXPECT_TRUE(node_b_focus.value());
}
}  // namespace
