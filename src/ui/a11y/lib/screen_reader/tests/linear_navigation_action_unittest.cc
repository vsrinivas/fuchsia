// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/linear_navigation_action.h"

#include <cstdint>
#include <memory>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/screen_reader_action_test_fixture.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;

const std::string kRootNodeLabel = "Label A";
const std::string kChildNodeLabel = "Label B";
constexpr uint32_t kRootNodeId = 0;
constexpr uint32_t kChildNodeId = 1;

constexpr auto kNextAction = a11y::LinearNavigationAction::LinearNavigationDirection::kNextAction;
constexpr auto kPreviousAction =
    a11y::LinearNavigationAction::LinearNavigationDirection::kPreviousAction;

class LinearNavigationActionTest : public ScreenReaderActionTest {
 public:
  LinearNavigationActionTest() = default;
  ~LinearNavigationActionTest() override = default;

  void AddNodeToSemanticTree() {
    // Creating test nodes to update.
    Node root_node = CreateTestNode(kRootNodeId, kRootNodeLabel);
    root_node.set_child_ids({kChildNodeId});

    Node child_node = CreateTestNode(kChildNodeId, kChildNodeLabel);

    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                                std::move(root_node));
    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                                std::move(child_node));
  }
};

// Linear Navigation Action should do nothing if there is no semantic tree in focus.
TEST_F(LinearNavigationActionTest, NoTreeInFocus) {
  a11y::LinearNavigationAction next_action(action_context(), mock_screen_reader_context(),
                                           kNextAction);

  // Call NextAction Run().
  next_action.Run({});
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_a11y_focus_manager()->IsGetA11yFocusCalled());
  EXPECT_FALSE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->message_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->message_ids()[0], fuchsia::intl::l10n::MessageIds::NO_FOCUS_ALERT);
}

// When next node is not found, the Linear Navigation Action should do nothing.
TEST_F(LinearNavigationActionTest, NextNodeNotFound) {
  AddNodeToSemanticTree();

  // Update focused node.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), kRootNodeId);

  mock_semantics_source()->set_get_next_node_should_fail(true);

  a11y::LinearNavigationAction next_action(action_context(), mock_screen_reader_context(),
                                           kNextAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Call NextAction Run().
  next_action.Run(gesture_context);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->message_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->message_ids()[0], fuchsia::intl::l10n::MessageIds::LAST_ELEMENT);
}

// When Previous node is not found, the Linear Navigation Action should do nothing.
TEST_F(LinearNavigationActionTest, PreviousNodeNotFound) {
  AddNodeToSemanticTree();

  // Update focused node.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), kRootNodeId);

  mock_semantics_source()->set_get_previous_node_should_fail(true);

  a11y::LinearNavigationAction previous_action(action_context(), mock_screen_reader_context(),
                                               kPreviousAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Call PreviousAction Run().
  previous_action.Run(gesture_context);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->message_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->message_ids()[0], fuchsia::intl::l10n::MessageIds::FIRST_ELEMENT);
}

// When SetA11yFocus fails then LinearNavigationAction should not call TTS to speak.
TEST_F(LinearNavigationActionTest, SetA11yFocusFailed) {
  uint32_t node_id = 0;
  AddNodeToSemanticTree();

  // Update focused node.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), node_id);

  // Update SetA11yFocus() callback status to fail.
  mock_a11y_focus_manager()->set_should_set_a11y_focus_fail(true);

  a11y::LinearNavigationAction next_action(action_context(), mock_screen_reader_context(),
                                           kNextAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Call NextAction Run().
  next_action.Run(gesture_context);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_EQ(kRootNodeId, mock_a11y_focus_manager()->GetA11yFocus().value().node_id);
  EXPECT_EQ(mock_semantic_provider()->koid(),
            mock_a11y_focus_manager()->GetA11yFocus().value().view_ref_koid);
  EXPECT_FALSE(mock_speaker()->ReceivedSpeak());
}

// NextAction should get focused node information and then call GetNextNode() to get the next node.
// Next action should then set focus to the new node and then read the label of the new node in
// focus using tts.
TEST_F(LinearNavigationActionTest, NextActionPerformed) {
  AddNodeToSemanticTree();

  // Update focused node.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), kRootNodeId);

  a11y::LinearNavigationAction next_action(action_context(), mock_screen_reader_context(),
                                           kNextAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Call NextAction Run().
  next_action.Run(gesture_context);
  RunLoopUntilIdle();

  const auto& requested_actions =
      mock_semantics_source()->GetRequestedActionsForView(mock_semantic_provider()->koid());
  EXPECT_EQ(requested_actions.size(), 1u);
  EXPECT_EQ(requested_actions[0].first, kChildNodeId);
  ASSERT_EQ(requested_actions[0].second, fuchsia::accessibility::semantics::Action::SHOW_ON_SCREEN);
  ASSERT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_EQ(kChildNodeId, mock_a11y_focus_manager()->GetA11yFocus().value().node_id);
  EXPECT_EQ(mock_semantic_provider()->koid(),
            mock_a11y_focus_manager()->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->node_ids()[0], kChildNodeId);
}

// Previous action should get focused node information and then call GetPrevousNode() to get the
// previous node. Previous action should then set focus to the new node and then read the label of
// the new node in focus using tts.
TEST_F(LinearNavigationActionTest, PreviousActionPerformed) {
  AddNodeToSemanticTree();

  // Update focused node.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), kChildNodeId);

  a11y::LinearNavigationAction previous_action(action_context(), mock_screen_reader_context(),
                                               kPreviousAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Call PreviousAction Run().
  previous_action.Run(gesture_context);
  RunLoopUntilIdle();

  const auto& requested_actions =
      mock_semantics_source()->GetRequestedActionsForView(mock_semantic_provider()->koid());
  EXPECT_EQ(requested_actions.size(), 1u);
  EXPECT_EQ(requested_actions[0].first, kRootNodeId);
  ASSERT_EQ(requested_actions[0].second, fuchsia::accessibility::semantics::Action::SHOW_ON_SCREEN);
  ASSERT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_EQ(mock_a11y_focus_manager()->GetA11yFocus().value().node_id, kRootNodeId);
  EXPECT_EQ(mock_semantic_provider()->koid(),
            mock_a11y_focus_manager()->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->node_ids()[0], kRootNodeId);
}

}  // namespace
}  // namespace accessibility_test
