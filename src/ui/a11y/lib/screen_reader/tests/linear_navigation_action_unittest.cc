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
  ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], kChildNodeId);
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
  ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], kRootNodeId);
}

TEST_F(LinearNavigationActionTest, LinearNavigationSkipsRedundantNodes) {
  Node root_node = CreateTestNode(0u, "root");
  root_node.set_child_ids({1u, 4u});

  Node node_1 = CreateTestNode(1u, "repeated node");
  node_1.set_child_ids({2u});

  Node node_2 = CreateTestNode(2u, "repeated node");
  node_2.set_child_ids({3u});

  Node node_3 = CreateTestNode(3u, "repeated node");

  Node node_4 = CreateTestNode(4u, "non repeated node");

  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(root_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node_1));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node_2));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node_3));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node_4));

  // Focus the root node.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), kRootNodeId);

  // Navigate forward.
  {
    a11y::LinearNavigationAction action(action_context(), mock_screen_reader_context(),
                                        kNextAction);
    a11y::GestureContext gesture_context;
    gesture_context.view_ref_koid = mock_semantic_provider()->koid();
    action.Run(gesture_context);
    RunLoopUntilIdle();

    // We end up on node 1.
    EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
    ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
    EXPECT_EQ(mock_speaker()->speak_node_ids().back(), 1u);
  }

  // Navigate forward.
  {
    a11y::LinearNavigationAction action(action_context(), mock_screen_reader_context(),
                                        kNextAction);
    a11y::GestureContext gesture_context;
    gesture_context.view_ref_koid = mock_semantic_provider()->koid();
    action.Run(gesture_context);
    RunLoopUntilIdle();

    // Nodes 2 and 3 are skipped, we end up on node 4.
    EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
    ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 2u);
    EXPECT_EQ(mock_speaker()->speak_node_ids().back(), 4u);
  }

  // Navigate backward.
  {
    a11y::LinearNavigationAction action(action_context(), mock_screen_reader_context(),
                                        kPreviousAction);
    a11y::GestureContext gesture_context;
    gesture_context.view_ref_koid = mock_semantic_provider()->koid();
    action.Run(gesture_context);
    RunLoopUntilIdle();

    // Nodes 2 and 3 are skipped, we end up back on node 1.
    EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
    ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 3u);
    EXPECT_EQ(mock_speaker()->speak_node_ids().back(), 1u);
  }
}

TEST_F(LinearNavigationActionTest, NextActionEntersTable) {
  Node root_node = CreateTestNode(0u, "root");
  root_node.set_child_ids({1u});

  Node child_node = CreateTestNode(1u, "child");
  child_node.set_child_ids({2u});

  Node table_node = CreateTestNode(2u, "table");
  table_node.set_role(fuchsia::accessibility::semantics::Role::TABLE);
  auto* table_attributes = table_node.mutable_attributes()->mutable_table_attributes();
  table_attributes->set_number_of_rows(3u);
  table_attributes->set_number_of_columns(4u);
  table_attributes->set_row_header_ids({5u, 7u});
  table_attributes->set_column_header_ids({6u, 8u});
  table_node.set_child_ids({3u, 4u, 5u, 6u, 7u, 8u});

  Node cell_node = CreateTestNode(3u, "cell 1");
  cell_node.set_role(fuchsia::accessibility::semantics::Role::CELL);
  auto* cell_attributes = cell_node.mutable_attributes()->mutable_table_cell_attributes();
  cell_attributes->set_row_index(0u);
  cell_attributes->set_column_index(0u);

  Node cell_node_2 = CreateTestNode(4u, "cell 2");
  cell_node_2.set_role(fuchsia::accessibility::semantics::Role::CELL);
  auto* cell_attributes_2 = cell_node_2.mutable_attributes()->mutable_table_cell_attributes();
  cell_attributes_2->set_row_index(0u);
  cell_attributes_2->set_column_index(1u);

  Node row_1_header_node = CreateTestNode(5u, "row 1 header");
  row_1_header_node.set_role(fuchsia::accessibility::semantics::Role::ROW_HEADER);

  Node column_1_header_node = CreateTestNode(6u, "column 1 header");
  column_1_header_node.set_role(fuchsia::accessibility::semantics::Role::COLUMN_HEADER);

  Node row_2_header_node = CreateTestNode(7u, "row 2 header");
  row_2_header_node.set_role(fuchsia::accessibility::semantics::Role::ROW_HEADER);

  Node column_2_header_node = CreateTestNode(8u, "column 2 header");
  column_2_header_node.set_role(fuchsia::accessibility::semantics::Role::COLUMN_HEADER);

  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(root_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(child_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(table_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(cell_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(cell_node_2));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(row_1_header_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(column_1_header_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(row_2_header_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(column_2_header_node));

  // Update focused node.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), 1u);

  // Navigate from node 1 -> node 3, entering the table (node 2).
  a11y::LinearNavigationAction next_action(action_context(), mock_screen_reader_context(),
                                           kNextAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Call NextAction Run().
  next_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_EQ(mock_a11y_focus_manager()->GetA11yFocus().value().node_id, 3u);
  EXPECT_EQ(mock_semantic_provider()->koid(),
            mock_a11y_focus_manager()->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 3u);
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].changed_table_cell_context->row_header,
            "row 1 header");
  EXPECT_EQ(
      mock_speaker()->speak_node_message_contexts()[0].changed_table_cell_context->column_header,
      "column 1 header");
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts()[0].entered_containers.size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].entered_containers[0]->node_id(), 2u);
  ASSERT_TRUE(mock_speaker()->speak_node_message_contexts()[0].exited_containers.empty());

  // Navigate to the next table cell.
  a11y::LinearNavigationAction next_action_2(action_context(), mock_screen_reader_context(),
                                             kNextAction);
  a11y::GestureContext gesture_context_2;
  gesture_context_2.view_ref_koid = mock_semantic_provider()->koid();
  next_action_2.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 2u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[1], 4u);
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts().size(), 2u);
  EXPECT_TRUE(mock_speaker()
                  ->speak_node_message_contexts()[1]
                  .changed_table_cell_context->row_header.empty());
  EXPECT_EQ(
      mock_speaker()->speak_node_message_contexts()[1].changed_table_cell_context->column_header,
      "column 2 header");
  ASSERT_TRUE(mock_speaker()->speak_node_message_contexts()[1].entered_containers.empty());
  ASSERT_TRUE(mock_speaker()->speak_node_message_contexts()[0].exited_containers.empty());
}

TEST_F(LinearNavigationActionTest, PreviousActionExitsTable) {
  Node root_node = CreateTestNode(0u, "root");
  root_node.set_child_ids({1u});

  Node child_node = CreateTestNode(1u, "child");
  child_node.set_child_ids({2u});

  Node table_node = CreateTestNode(2u, "table");
  table_node.set_role(fuchsia::accessibility::semantics::Role::TABLE);
  auto* table_attributes = table_node.mutable_attributes()->mutable_table_attributes();
  table_attributes->set_number_of_rows(3u);
  table_attributes->set_number_of_columns(4u);
  table_node.set_child_ids({3u});
  EXPECT_TRUE(table_node.has_attributes());
  EXPECT_TRUE(table_node.attributes().has_table_attributes());

  Node cell_node = CreateTestNode(3u, "cell");
  cell_node.set_role(fuchsia::accessibility::semantics::Role::CELL);
  auto* cell_attributes = table_node.mutable_attributes()->mutable_table_cell_attributes();
  cell_attributes->set_row_index(0u);
  cell_attributes->set_column_index(1u);

  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(root_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(child_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(table_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(cell_node));

  // Update focused node.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), 3u);

  a11y::ScreenReaderContext::NavigationContext navigation_context;
  navigation_context.containers = {{.node_id = 2u, .table_context = {}}};
  navigation_context.view_ref_koid = mock_semantic_provider()->koid();
  mock_screen_reader_context()->set_current_navigation_context(navigation_context);

  a11y::LinearNavigationAction previous_action(action_context(), mock_screen_reader_context(),
                                               kPreviousAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  // Call NextAction Run().
  previous_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_EQ(mock_a11y_focus_manager()->GetA11yFocus().value().node_id, 1u);
  EXPECT_EQ(mock_semantic_provider()->koid(),
            mock_a11y_focus_manager()->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  EXPECT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 1u);
  ASSERT_TRUE(mock_speaker()->speak_node_message_contexts()[0].entered_containers.empty());
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts()[0].exited_containers.size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].exited_containers[0]->node_id(), 2u);
}

TEST_F(LinearNavigationActionTest, NextActionEntersNestedTable) {
  Node root_node = CreateTestNode(0u, "root");
  root_node.set_child_ids({1u});

  Node child_node = CreateTestNode(1u, "child");
  child_node.set_child_ids({2u});

  Node table_node = CreateTestNode(2u, "table");
  table_node.set_role(fuchsia::accessibility::semantics::Role::TABLE);
  auto* table_attributes = table_node.mutable_attributes()->mutable_table_attributes();
  table_attributes->set_number_of_rows(3u);
  table_attributes->set_number_of_columns(4u);
  table_node.set_child_ids({3u});
  EXPECT_TRUE(table_node.has_attributes());
  EXPECT_TRUE(table_node.attributes().has_table_attributes());

  Node cell_node = CreateTestNode(3u, "cell");
  cell_node.set_role(fuchsia::accessibility::semantics::Role::CELL);
  auto* cell_attributes = table_node.mutable_attributes()->mutable_table_cell_attributes();
  cell_attributes->set_row_index(0u);
  cell_attributes->set_column_index(1u);
  cell_node.set_child_ids({4u});

  Node nested_table_node = CreateTestNode(4u, "nested table");
  nested_table_node.set_role(fuchsia::accessibility::semantics::Role::TABLE);
  nested_table_node.set_child_ids({5u});

  Node nested_table_cell_node = CreateTestNode(5u, "nested table cell");
  nested_table_cell_node.set_role(fuchsia::accessibility::semantics::Role::CELL);

  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(root_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(child_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(table_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(cell_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(nested_table_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(nested_table_cell_node));

  // Update focused node.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), 3u);

  a11y::ScreenReaderContext::NavigationContext navigation_context;
  navigation_context.containers = {{.node_id = 2u, .table_context = {}}};
  navigation_context.view_ref_koid = mock_semantic_provider()->koid();
  mock_screen_reader_context()->set_current_navigation_context(navigation_context);

  a11y::LinearNavigationAction next_action(action_context(), mock_screen_reader_context(),
                                           kNextAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  next_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_EQ(mock_a11y_focus_manager()->GetA11yFocus().value().node_id, 5u);
  EXPECT_EQ(mock_semantic_provider()->koid(),
            mock_a11y_focus_manager()->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  EXPECT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 5u);
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts()[0].entered_containers.size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].entered_containers[0]->node_id(), 4u);
  ASSERT_TRUE(mock_speaker()->speak_node_message_contexts()[0].exited_containers.empty());
}

TEST_F(LinearNavigationActionTest, PreviousActionExitsNestedTable) {
  Node root_node = CreateTestNode(0u, "root");
  root_node.set_child_ids({1u});

  Node child_node = CreateTestNode(1u, "child");
  child_node.set_child_ids({2u});

  Node table_node = CreateTestNode(2u, "table");
  table_node.set_role(fuchsia::accessibility::semantics::Role::TABLE);
  auto* table_attributes = table_node.mutable_attributes()->mutable_table_attributes();
  table_attributes->set_number_of_rows(3u);
  table_attributes->set_number_of_columns(4u);
  table_node.set_child_ids({3u});
  EXPECT_TRUE(table_node.has_attributes());
  EXPECT_TRUE(table_node.attributes().has_table_attributes());

  Node cell_node = CreateTestNode(3u, "cell");
  cell_node.set_role(fuchsia::accessibility::semantics::Role::CELL);
  auto* cell_attributes = table_node.mutable_attributes()->mutable_table_cell_attributes();
  cell_attributes->set_row_index(0u);
  cell_attributes->set_column_index(1u);
  cell_node.set_child_ids({4u});

  Node nested_table_node = CreateTestNode(4u, "nested table");
  nested_table_node.set_role(fuchsia::accessibility::semantics::Role::TABLE);
  nested_table_node.set_child_ids({5u});

  Node nested_table_cell_node = CreateTestNode(5u, "nested table cell");
  nested_table_cell_node.set_role(fuchsia::accessibility::semantics::Role::CELL);

  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(root_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(child_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(table_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(cell_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(nested_table_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(nested_table_cell_node));

  // Update focused node.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), 5u);

  a11y::ScreenReaderContext::NavigationContext navigation_context;
  navigation_context.containers = {{.node_id = 2u}, {.node_id = 4u, .table_context = {}}};
  navigation_context.view_ref_koid = mock_semantic_provider()->koid();
  mock_screen_reader_context()->set_current_navigation_context(navigation_context);

  a11y::LinearNavigationAction previous_action(action_context(), mock_screen_reader_context(),
                                               kPreviousAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  previous_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_EQ(mock_a11y_focus_manager()->GetA11yFocus().value().node_id, 3u);
  EXPECT_EQ(mock_semantic_provider()->koid(),
            mock_a11y_focus_manager()->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  EXPECT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 3u);
  ASSERT_TRUE(mock_speaker()->speak_node_message_contexts()[0].entered_containers.empty());
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts()[0].exited_containers.size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].exited_containers[0]->node_id(), 4u);
}

TEST_F(LinearNavigationActionTest, NextActionEntersList) {
  Node root_node = CreateTestNode(0u, "root");
  root_node.set_child_ids({1u, 2u, 5u});

  Node node_before_list = CreateTestNode(1u, "node before list");
  node_before_list.set_child_ids({2u});

  Node list_node = CreateTestNode(2u, "list");
  list_node.set_role(fuchsia::accessibility::semantics::Role::LIST);
  list_node.set_child_ids({3u});

  // We use an empty label so that node won't be read - this is a common pattern in practice,
  // e.g.
  // https://source.chromium.org/chromium/chromium/src/+/main:content/test/data/accessibility/html/list-expected-fuchsia.txt;l=1;drc=504fbe94c3c2ad609aabc12f3958e4aa16a0d2e0
  Node list_element_node = CreateTestNode(3u, std::nullopt);
  list_element_node.set_role(fuchsia::accessibility::semantics::Role::LIST_ELEMENT);
  list_element_node.set_child_ids({4u});

  Node static_text_node = CreateTestNode(4u, "static text node inside list");

  Node node_after_list = CreateTestNode(5u, "node after list");
  node_after_list.set_child_ids({5u});

  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(root_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(node_before_list));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(list_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(list_element_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(static_text_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(node_after_list));

  // Start at the element before the list.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), 1u);

  // Navigate from node 1 -> node 4, entering the list.
  a11y::LinearNavigationAction next_action(action_context(), mock_screen_reader_context(),
                                           kNextAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  next_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_EQ(mock_a11y_focus_manager()->GetA11yFocus().value().node_id, 4u);
  EXPECT_EQ(mock_semantic_provider()->koid(),
            mock_a11y_focus_manager()->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 4u);
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts().size(), 1u);
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts()[0].entered_containers.size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].entered_containers[0]->node_id(), 2u);
  ASSERT_TRUE(mock_speaker()->speak_node_message_contexts()[0].exited_containers.empty());
}

TEST_F(LinearNavigationActionTest, NextActionExitsList) {
  Node root_node = CreateTestNode(0u, "root");
  root_node.set_child_ids({1u, 2u, 5u});

  Node node_before_list = CreateTestNode(1u, "node before list");
  node_before_list.set_child_ids({2u});

  Node list_node = CreateTestNode(2u, "list");
  list_node.set_role(fuchsia::accessibility::semantics::Role::LIST);
  list_node.set_child_ids({3u});

  // We use an empty label so that node won't be read - this is a common pattern in practice,
  // e.g.
  // https://source.chromium.org/chromium/chromium/src/+/main:content/test/data/accessibility/html/list-expected-fuchsia.txt;l=1;drc=504fbe94c3c2ad609aabc12f3958e4aa16a0d2e0
  Node list_element_node = CreateTestNode(3u, std::nullopt);
  list_element_node.set_role(fuchsia::accessibility::semantics::Role::LIST_ELEMENT)
      .set_child_ids({4u});

  Node static_text_node = CreateTestNode(4u, "static text node inside list");

  Node node_after_list = CreateTestNode(5u, "node after list");

  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(root_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(node_before_list));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(list_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(list_element_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(static_text_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(node_after_list));

  // Start inside the list.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), 4u);
  a11y::ScreenReaderContext::NavigationContext navigation_context;
  navigation_context.containers = {{.node_id = 2u, .table_context = {}}};
  navigation_context.view_ref_koid = mock_semantic_provider()->koid();
  mock_screen_reader_context()->set_current_navigation_context(navigation_context);

  // Navigate from node 4 -> node 5, entering the list.
  a11y::LinearNavigationAction next_action(action_context(), mock_screen_reader_context(),
                                           kNextAction);
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  next_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_EQ(mock_a11y_focus_manager()->GetA11yFocus().value().node_id, 5u);
  EXPECT_EQ(mock_semantic_provider()->koid(),
            mock_a11y_focus_manager()->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 5u);
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts().size(), 1u);
  ASSERT_TRUE(mock_speaker()->speak_node_message_contexts()[0].entered_containers.empty());
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts()[0].exited_containers.size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].exited_containers[0]->node_id(), 2u);
}

}  // namespace
}  // namespace accessibility_test
