// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/change_range_value_action.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <cstdint>
#include <utility>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {
namespace {

using ChangeRangeValueActionType = a11y::ChangeRangeValueAction::ChangeRangeValueActionType;

using fuchsia::accessibility::semantics::Action;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;
using fuchsia::accessibility::semantics::States;

const std::string kRootNodeLabel = "Label A";
constexpr uint32_t kRootNodeId = 0;
constexpr uint32_t kSliderDelta = 10;
constexpr uint32_t kSliderIntialRangeValue = 40;

class ChangeRangeValueActionTest : public gtest::TestLoopFixture {
 public:
  ChangeRangeValueActionTest()
      : view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                      std::make_unique<MockViewSemanticsFactory>(),
                      std::make_unique<MockAnnotationViewFactory>(), context_provider_.context(),
                      context_provider_.context()->outgoing()->debug_dir()),
        semantic_provider_(&view_manager_) {
    action_context_.semantics_source = &view_manager_;

    screen_reader_context_ = std::make_unique<MockScreenReaderContext>();
    a11y_focus_manager_ptr_ = screen_reader_context_->mock_a11y_focus_manager_ptr();
    mock_speaker_ptr_ = screen_reader_context_->mock_speaker_ptr();
    view_manager_.SetSemanticsEnabled(true);
  }

  void SetUp() override {
    Node node = CreateSliderNode(kRootNodeId, kRootNodeLabel);
    std::vector<Node> update_nodes;
    update_nodes.push_back(std::move(node));

    // Update the node created above.
    semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();

    // Commit nodes.
    semantic_provider_.CommitUpdates();
    RunLoopUntilIdle();

    semantic_provider_.SetRequestedAction(fuchsia::accessibility::semantics::Action::SET_FOCUS);

    // Update focused node.
    a11y_focus_manager_ptr_->SetA11yFocus(semantic_provider_.koid(), kRootNodeId,
                                          [](bool result) { EXPECT_TRUE(result); });

    semantic_provider_.SetSliderDelta(kSliderDelta);

    // Update slider node in semantic_provider_ which will be used to send slider update with new
    // |range_value|.
    {
      Node node = CreateSliderNode(kRootNodeId, kRootNodeLabel);
      // Update the node created above.
      semantic_provider_.SetSliderNode(std::move(node));
    }
  }

  static Node CreateSliderNode(uint32_t node_id, std::string label) {
    Node node = CreateTestNode(node_id, std::move(label));
    node.set_role(Role::SLIDER);
    States state;
    state.set_range_value(kSliderIntialRangeValue);
    node.set_states(std::move(state));
    return node;
  }

  sys::testing::ComponentContextProvider context_provider_;
  a11y::ViewManager view_manager_;
  a11y::ScreenReaderAction::ActionContext action_context_;
  std::unique_ptr<MockScreenReaderContext> screen_reader_context_;
  MockA11yFocusManager* a11y_focus_manager_ptr_;
  MockScreenReaderContext::MockSpeaker* mock_speaker_ptr_;
  accessibility_test::MockSemanticProvider semantic_provider_;
};

// Tests the scenario where no tree is in focus.
TEST_F(ChangeRangeValueActionTest, NoTreeInFocus) {
  a11y::ScreenReaderContext* context = screen_reader_context_.get();
  a11y::ChangeRangeValueAction range_value_action(&action_context_, context,
                                                  ChangeRangeValueActionType::kIncrementAction);
  a11y::ChangeRangeValueAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();

  // Update A11y Focus Manager to return invalid a11y focus.
  a11y_focus_manager_ptr_->set_should_get_a11y_focus_fail(true);

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(action_data);
  RunLoopUntilIdle();

  ASSERT_TRUE(a11y_focus_manager_ptr_->IsGetA11yFocusCalled());
  ASSERT_FALSE(semantic_provider_.OnAccessibilityActionRequestedCalled());
  EXPECT_FALSE(mock_speaker_ptr_->ReceivedSpeak());
}

// Tests the scenario where the A11y focused node is not found.
TEST_F(ChangeRangeValueActionTest, FocusedNodeNotFound) {
  a11y::ScreenReaderContext* context = screen_reader_context_.get();
  a11y::ChangeRangeValueAction range_value_action(&action_context_, context,
                                                  ChangeRangeValueActionType::kIncrementAction);
  a11y::ChangeRangeValueAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();

  // Update focused node to an invalid node_id.
  uint32_t invalid_node_id = 100;
  a11y_focus_manager_ptr_->SetA11yFocus(semantic_provider_.koid(), invalid_node_id,
                                        [](bool result) { EXPECT_TRUE(result); });

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(action_data);
  RunLoopUntilIdle();

  ASSERT_TRUE(a11y_focus_manager_ptr_->IsGetA11yFocusCalled());
  ASSERT_FALSE(semantic_provider_.OnAccessibilityActionRequestedCalled());
  EXPECT_FALSE(mock_speaker_ptr_->ReceivedSpeak());
}

// Tests the scenario when the call to OnAccessibilityActionRequested() fails.
TEST_F(ChangeRangeValueActionTest, OnAccessibilityActionRequestedFailed) {
  a11y::ScreenReaderContext* context = screen_reader_context_.get();
  a11y::ChangeRangeValueAction range_value_action(&action_context_, context,
                                                  ChangeRangeValueActionType::kIncrementAction);
  a11y::ChangeRangeValueAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();

  // Update semantic provider so that a call to OnAccessibilityActionRequested() results in failure.
  semantic_provider_.SetOnAccessibilityActionCallbackStatus(false);

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(action_data);
  RunLoopUntilIdle();

  ASSERT_TRUE(a11y_focus_manager_ptr_->IsGetA11yFocusCalled());
  ASSERT_TRUE(semantic_provider_.OnAccessibilityActionRequestedCalled());
  ASSERT_EQ(Action::INCREMENT, semantic_provider_.GetRequestedAction());
  EXPECT_EQ(kRootNodeId, semantic_provider_.GetRequestedActionNodeId());
  EXPECT_FALSE(mock_speaker_ptr_->ReceivedSpeak());
}

// Tests the scenario when the Range control is incremented.
TEST_F(ChangeRangeValueActionTest, RangeControlIncremented) {
  a11y::ScreenReaderContext* context = screen_reader_context_.get();
  a11y::ChangeRangeValueAction range_value_action(&action_context_, context,
                                                  ChangeRangeValueActionType::kIncrementAction);
  a11y::ChangeRangeValueAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(action_data);
  RunLoopUntilIdle();

  ASSERT_TRUE(a11y_focus_manager_ptr_->IsGetA11yFocusCalled());
  ASSERT_TRUE(semantic_provider_.OnAccessibilityActionRequestedCalled());
  ASSERT_EQ(Action::INCREMENT, semantic_provider_.GetRequestedAction());
  EXPECT_EQ(kRootNodeId, semantic_provider_.GetRequestedActionNodeId());
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->messages().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->messages()[0],
            std::to_string(kSliderDelta + kSliderIntialRangeValue));
}

// Tests the scenario when the Range control is decremented.
TEST_F(ChangeRangeValueActionTest, RangeControlDecremented) {
  a11y::ScreenReaderContext* context = screen_reader_context_.get();
  a11y::ChangeRangeValueAction range_value_action(&action_context_, context,
                                                  ChangeRangeValueActionType::kDecrementAction);
  a11y::ChangeRangeValueAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(action_data);
  RunLoopUntilIdle();

  ASSERT_TRUE(a11y_focus_manager_ptr_->IsGetA11yFocusCalled());
  ASSERT_TRUE(semantic_provider_.OnAccessibilityActionRequestedCalled());
  ASSERT_EQ(Action::DECREMENT, semantic_provider_.GetRequestedAction());
  EXPECT_EQ(kRootNodeId, semantic_provider_.GetRequestedActionNodeId());
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->messages().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->messages()[0],
            std::to_string(kSliderDelta + kSliderIntialRangeValue));
}

}  // namespace
}  // namespace accessibility_test
