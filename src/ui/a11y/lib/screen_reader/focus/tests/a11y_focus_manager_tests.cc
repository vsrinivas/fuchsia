// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/ui/a11y/lib/annotation/tests/mocks/mock_focus_highlight_manager.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_registry.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"
#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "src/ui/a11y/lib/testing/view_ref_helper.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {
namespace {

class A11yFocusManagerTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    a11y_focus_manager_ = std::make_unique<a11y::A11yFocusManager>(
        &mock_focus_chain_requester_, &mock_focus_chain_registry_, &mock_focus_highlight_manager_);
  }

  // Helper function to check if the given ViewRef has a11y focus.
  void CheckViewInFocus(const ViewRefHelper& view_ref_helper, uint32_t node_id) const {
    auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
    ASSERT_TRUE(a11y_focus.has_value());
    EXPECT_EQ(view_ref_helper.koid(), a11y_focus.value().view_ref_koid);
    EXPECT_EQ(node_id, a11y_focus.value().node_id);
    auto highlighted_node = mock_focus_highlight_manager_.GetHighlightedNode();
    EXPECT_EQ(highlighted_node->koid, view_ref_helper.koid());
    EXPECT_EQ(highlighted_node->node_id, node_id);
  }

  MockAccessibilityFocusChainRequester mock_focus_chain_requester_;
  MockAccessibilityFocusChainRegistry mock_focus_chain_registry_;
  MockFocusHighlightManager mock_focus_highlight_manager_;
  std::unique_ptr<a11y::A11yFocusManager> a11y_focus_manager_;
};

// GetA11yFocus() doesn't return anything when no view is in focus.
TEST_F(A11yFocusManagerTest, GetA11yFocusNoViewFound) {
  // By default no view is in a11y focus.
  auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  ASSERT_FALSE(a11y_focus.has_value());
}

TEST_F(A11yFocusManagerTest, ChangingA11yFocusCausesAFocusChainUpdate) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  ViewRefHelper view_ref_helper;
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper.koid(), a11y::A11yFocusManager::kRootNodeId,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper, a11y::A11yFocusManager::kRootNodeId);
  EXPECT_TRUE(success);
  // Now that one view is in Focus, changes the focus to another view, which causes again a Focus
  // Chain update.
  mock_focus_chain_requester_.set_will_change_focus(true);
  ViewRefHelper view_ref_helper_2;
  bool success_2 = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_2.koid(), 1u,
                                    [&success_2](bool result) { success_2 = result; });
  CheckViewInFocus(view_ref_helper_2, 1u);
  EXPECT_TRUE(success_2);
}

TEST_F(A11yFocusManagerTest, ChangingA11yFocusCausesAFailedFocusChainUpdate) {
  mock_focus_chain_requester_.set_will_change_focus(false);
  ViewRefHelper view_ref_helper;
  bool success = true;  // expects false later.
  a11y_focus_manager_->SetA11yFocus(view_ref_helper.koid(), a11y::A11yFocusManager::kRootNodeId,
                                    [&success](bool result) { success = result; });
  EXPECT_FALSE(success);
  auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  ASSERT_FALSE(a11y_focus.has_value());
}

TEST_F(A11yFocusManagerTest, ChangingA11yFocusToTheSameView) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  ViewRefHelper view_ref_helper;
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper.koid(), a11y::A11yFocusManager::kRootNodeId,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper, a11y::A11yFocusManager::kRootNodeId);
  EXPECT_TRUE(success);

  // Changes the focus to the same view.
  mock_focus_chain_requester_.set_will_change_focus(true);
  bool success_2 = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper.koid(), 1u,
                                    [&success_2](bool result) { success_2 = result; });
  CheckViewInFocus(view_ref_helper, 1u);
  EXPECT_TRUE(success_2);
}

TEST_F(A11yFocusManagerTest, ListensToFocusChainUpdates) {
  ViewRefHelper view_ref_helper;
  // The Focus Chain is updated and the Focus Chain Manager listens to the update.
  mock_focus_chain_registry_.SendViewRefKoid(view_ref_helper.koid());
  CheckViewInFocus(view_ref_helper, a11y::A11yFocusManager::kRootNodeId);
}

TEST_F(A11yFocusManagerTest, ClearsTheA11YFocus) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  ViewRefHelper view_ref_helper;
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper.koid(), a11y::A11yFocusManager::kRootNodeId,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper, a11y::A11yFocusManager::kRootNodeId);
  EXPECT_TRUE(success);

  a11y_focus_manager_->ClearA11yFocus();
  auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  ASSERT_FALSE(a11y_focus);
  auto highlighted_node = mock_focus_highlight_manager_.GetHighlightedNode();
  ASSERT_FALSE(highlighted_node);
}

}  // namespace
}  // namespace accessibility_test
