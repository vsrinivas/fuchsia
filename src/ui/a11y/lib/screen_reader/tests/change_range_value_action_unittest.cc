// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/change_range_value_action.h"

#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <utility>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/screen_reader_action_test_fixture.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"

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

class ChangeRangeValueActionTest : public ScreenReaderActionTest {
 public:
  ChangeRangeValueActionTest() = default;
  ~ChangeRangeValueActionTest() = default;

  void SetUp() override {
    ScreenReaderActionTest::SetUp();

    // Create test slider node
    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                                CreateSliderNode(kRootNodeId, kRootNodeLabel));

    // Update focused node.
    mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), kRootNodeId,
                                            [](bool result) { EXPECT_TRUE(result); });
  }

  static Node CreateSliderNode(uint32_t node_id, std::string label) {
    Node node = CreateTestNode(node_id, std::move(label));
    node.set_role(Role::SLIDER);
    States state;
    state.set_range_value(kSliderIntialRangeValue);
    node.set_states(std::move(state));
    return node;
  }

  void SetSliderRangeValue(uint32_t value, bool use_range_value = true) {
    // Update the slider node's range value so that we can verify that the new
    // value is used to produce the utterance.
    auto* node =
        mock_semantics_source()->GetSemanticNode(mock_semantic_provider()->koid(), kRootNodeId);
    fuchsia::accessibility::semantics::Node updated_node;
    if (node) {
      fidl::Clone(*node, &updated_node);
    }

    fuchsia::accessibility::semantics::States states;
    if (use_range_value) {
      states.set_range_value(value);
    } else {
      states.set_value(std::to_string(static_cast<int>(value)));
    }
    updated_node.set_states(std::move(states));
    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                                std::move(updated_node));
  }
};

// Tests the scenario where no tree is in focus.
TEST_F(ChangeRangeValueActionTest, NoTreeInFocus) {
  a11y::ScreenReaderContext* context = mock_screen_reader_context();
  a11y::ChangeRangeValueAction range_value_action(action_context(), context,
                                                  ChangeRangeValueActionType::kIncrementAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Update A11y Focus Manager to return invalid a11y focus.
  mock_a11y_focus_manager()->set_should_get_a11y_focus_fail(true);

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsGetA11yFocusCalled());
  const auto& requested_actions =
      mock_semantics_source()->GetRequestedActionsForView(mock_semantic_provider()->koid());
  EXPECT_TRUE(requested_actions.empty());
  EXPECT_FALSE(mock_screen_reader_context()->has_on_node_update_callback());
}

// Tests the scenario where the A11y focused node is not found.
TEST_F(ChangeRangeValueActionTest, FocusedNodeNotFound) {
  a11y::ScreenReaderContext* context = mock_screen_reader_context();
  a11y::ChangeRangeValueAction range_value_action(action_context(), context,
                                                  ChangeRangeValueActionType::kIncrementAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Update focused node to an invalid node_id.
  uint32_t invalid_node_id = 100;
  mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), invalid_node_id,
                                          [](bool result) { EXPECT_TRUE(result); });

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsGetA11yFocusCalled());
  const auto& requested_actions =
      mock_semantics_source()->GetRequestedActionsForView(mock_semantic_provider()->koid());
  EXPECT_TRUE(requested_actions.empty());
  EXPECT_FALSE(mock_screen_reader_context()->has_on_node_update_callback());
}

// Tests the scenario when the call to OnAccessibilityActionRequested() fails.
TEST_F(ChangeRangeValueActionTest, OnAccessibilityActionRequestedFailed) {
  a11y::ScreenReaderContext* context = mock_screen_reader_context();
  a11y::ChangeRangeValueAction range_value_action(action_context(), context,
                                                  ChangeRangeValueActionType::kIncrementAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Update semantics source so that a call to PerformAccessibilityAction() results in failure.
  mock_semantics_source()->set_perform_accessibility_action_callback_value(false);

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsGetA11yFocusCalled());
  const auto& requested_actions =
      mock_semantics_source()->GetRequestedActionsForView(mock_semantic_provider()->koid());
  EXPECT_EQ(requested_actions.size(), 1u);
  EXPECT_EQ(requested_actions[0].first, kRootNodeId);
  EXPECT_EQ(requested_actions[0].second, fuchsia::accessibility::semantics::Action::INCREMENT);
  EXPECT_FALSE(mock_screen_reader_context()->has_on_node_update_callback());
}

// Tests the scenario when the Range control is incremented.
TEST_F(ChangeRangeValueActionTest, RangeControlIncremented) {
  a11y::ScreenReaderContext* context = mock_screen_reader_context();
  a11y::ChangeRangeValueAction range_value_action(action_context(), context,
                                                  ChangeRangeValueActionType::kIncrementAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  mock_semantics_source()->set_custom_action_callback(
      [this]() { SetSliderRangeValue(kSliderIntialRangeValue + kSliderDelta); });

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsGetA11yFocusCalled());
  const auto& requested_actions =
      mock_semantics_source()->GetRequestedActionsForView(mock_semantic_provider()->koid());
  EXPECT_EQ(requested_actions.size(), 1u);
  EXPECT_EQ(requested_actions[0].first, kRootNodeId);
  EXPECT_EQ(requested_actions[0].second, fuchsia::accessibility::semantics::Action::INCREMENT);
  EXPECT_TRUE(mock_screen_reader_context()->has_on_node_update_callback());

  // Run the callback and check that the new value is read.
  mock_screen_reader_context()->run_and_clear_on_node_update_callback();
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->messages().size(), 1u);
  EXPECT_EQ(mock_speaker()->messages()[0], std::to_string(kSliderDelta + kSliderIntialRangeValue));
}

// Tests the scenario when the Range control is decremented.
TEST_F(ChangeRangeValueActionTest, RangeControlDecremented) {
  a11y::ScreenReaderContext* context = mock_screen_reader_context();
  a11y::ChangeRangeValueAction range_value_action(action_context(), context,
                                                  ChangeRangeValueActionType::kDecrementAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  mock_semantics_source()->set_custom_action_callback(
      [this]() { SetSliderRangeValue(kSliderIntialRangeValue - kSliderDelta); });

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsGetA11yFocusCalled());
  const auto& requested_actions =
      mock_semantics_source()->GetRequestedActionsForView(mock_semantic_provider()->koid());
  EXPECT_EQ(requested_actions.size(), 1u);
  EXPECT_EQ(requested_actions[0].first, kRootNodeId);
  EXPECT_EQ(requested_actions[0].second, fuchsia::accessibility::semantics::Action::DECREMENT);
  EXPECT_TRUE(mock_screen_reader_context()->has_on_node_update_callback());

  // Run the callback and check that the new value is read.
  mock_screen_reader_context()->run_and_clear_on_node_update_callback();
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->messages().size(), 1u);
  EXPECT_EQ(mock_speaker()->messages()[0], std::to_string(kSliderIntialRangeValue - kSliderDelta));
}

// Tests the scenario when the Range control is incremented.
TEST_F(ChangeRangeValueActionTest, RangeControlIncrementedUseValue) {
  a11y::ScreenReaderContext* context = mock_screen_reader_context();
  a11y::ChangeRangeValueAction range_value_action(action_context(), context,
                                                  ChangeRangeValueActionType::kIncrementAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Increment the slider value, but store the new value in the range field
  // instead of the range_value field.
  mock_semantics_source()->set_custom_action_callback([this]() {
    SetSliderRangeValue(kSliderIntialRangeValue + kSliderDelta, /* use_range_value = */ false);
  });

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(gesture_context);
  RunLoopUntilIdle();

  // Check that INCREMENT action was requested on the correct node.
  ASSERT_TRUE(mock_a11y_focus_manager()->IsGetA11yFocusCalled());
  const auto& requested_actions =
      mock_semantics_source()->GetRequestedActionsForView(mock_semantic_provider()->koid());
  EXPECT_EQ(requested_actions.size(), 1u);
  EXPECT_EQ(requested_actions[0].first, kRootNodeId);
  EXPECT_EQ(requested_actions[0].second, fuchsia::accessibility::semantics::Action::INCREMENT);
  EXPECT_TRUE(mock_screen_reader_context()->has_on_node_update_callback());

  // Run the callback and check that the new value is read.
  mock_screen_reader_context()->run_and_clear_on_node_update_callback();
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->messages().size(), 1u);
  EXPECT_EQ(mock_speaker()->messages()[0], std::to_string(kSliderIntialRangeValue + kSliderDelta));
}

// Tests the scenario when the focus changes before the action completes.
// In practice, this scenario is very unlikely, but we should still exercise
// this codepath in tests.
TEST_F(ChangeRangeValueActionTest, FocusChangesBeforeActionCompletes) {
  a11y::ScreenReaderContext* context = mock_screen_reader_context();
  a11y::ChangeRangeValueAction range_value_action(action_context(), context,
                                                  ChangeRangeValueActionType::kIncrementAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Increment the slider value, but store the new value in the range field
  // instead of the range_value field.
  mock_semantics_source()->set_custom_action_callback([this]() {
    SetSliderRangeValue(kSliderIntialRangeValue + kSliderDelta, /* use_range_value = */ false);
  });

  // Call ChangeRangeValueAction Run()
  range_value_action.Run(gesture_context);
  RunLoopUntilIdle();

  // Check that INCREMENT action was requested on the correct node.
  ASSERT_TRUE(mock_a11y_focus_manager()->IsGetA11yFocusCalled());
  const auto& requested_actions =
      mock_semantics_source()->GetRequestedActionsForView(mock_semantic_provider()->koid());
  EXPECT_EQ(requested_actions.size(), 1u);
  EXPECT_EQ(requested_actions[0].first, kRootNodeId);
  EXPECT_EQ(requested_actions[0].second, fuchsia::accessibility::semantics::Action::INCREMENT);
  EXPECT_TRUE(mock_screen_reader_context()->has_on_node_update_callback());

  // Change the focus.
  // Update focused node.
  mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), 1u,
                                          [](bool result) { EXPECT_TRUE(result); });

  // Run the callback and check that the new value is read.
  mock_screen_reader_context()->run_and_clear_on_node_update_callback();
  RunLoopUntilIdle();

  EXPECT_FALSE(mock_speaker()->ReceivedSpeak());
}

}  // namespace
}  // namespace accessibility_test
