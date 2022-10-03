// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/explore_action.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/accessibility/semantics/cpp/fidl.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/screen_reader_action_test_fixture.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Node;

// Arbitrary number to set a local coordinate when sending for hit testing.
static const int kLocalCoordForTesting = 10;

class ExploreActionTest : public ScreenReaderActionTest {
 public:
  ExploreActionTest() = default;
  ~ExploreActionTest() override = default;

  void SetUp() override {
    ScreenReaderActionTest::SetUp();

    mock_a11y_focus_manager()->SetA11yFocus(100u, 10000, [](bool result) { EXPECT_TRUE(result); });
    mock_a11y_focus_manager()->ResetExpectations();

    // Creating test node to update.
    Node node = accessibility_test::CreateTestNode(0, "Label A", {1u});
    Node node_2 = accessibility_test::CreateTestNode(1u, "", {});
    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node));
    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                                std::move(node_2));
  }
};

TEST_F(ExploreActionTest, SuccessfulExploreActionReadsNode) {
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  gesture_context.current_pointer_locations[0].local_point.x = kLocalCoordForTesting;
  gesture_context.current_pointer_locations[0].local_point.y = kLocalCoordForTesting;

  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(0u);
  mock_semantics_source()->SetHitTestResult(mock_semantic_provider()->koid(), std::move(hit));
  EXPECT_FALSE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());

  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  // Checks that a new a11y focus was set.
  EXPECT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  auto focus = mock_a11y_focus_manager()->GetA11yFocus();
  ASSERT_TRUE(focus);
  EXPECT_EQ(focus->node_id, 0u);
  EXPECT_EQ(focus->view_ref_koid, mock_semantic_provider()->koid());
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 0u);
}

TEST_F(ExploreActionTest, HitTestFails) {
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  gesture_context.current_pointer_locations[0].local_point.x = 10;
  gesture_context.current_pointer_locations[0].local_point.y = 10;

  // In order for the mock semantics source to return a hit test result, we need
  // to set it explicitly before we run the action. By leaving it unset, we
  // ensure that the hit test will not return a result.
  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  EXPECT_FALSE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_FALSE(mock_speaker()->ReceivedSpeak());
  ASSERT_TRUE(mock_speaker()->speak_node_ids().empty());
}

TEST_F(ExploreActionTest, SetA11yFocusFails) {
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  gesture_context.current_pointer_locations[0].local_point.x = 10;
  gesture_context.current_pointer_locations[0].local_point.y = 10;

  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(0u);
  mock_semantics_source()->SetHitTestResult(mock_semantic_provider()->koid(), std::move(hit));
  mock_a11y_focus_manager()->set_should_set_a11y_focus_fail(true);

  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  auto focus = mock_a11y_focus_manager()->GetA11yFocus();
  ASSERT_TRUE(focus);
  EXPECT_NE(focus->node_id, 0u);
  EXPECT_NE(focus->view_ref_koid, mock_semantic_provider()->koid());

  EXPECT_FALSE(mock_speaker()->ReceivedSpeak());
  ASSERT_TRUE(mock_speaker()->speak_node_ids().empty());
}

TEST_F(ExploreActionTest, GettingA11yFocusFails) {
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  gesture_context.current_pointer_locations[0].local_point.x = 10;
  gesture_context.current_pointer_locations[0].local_point.y = 10;

  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(0u);
  mock_semantics_source()->SetHitTestResult(mock_semantic_provider()->koid(), std::move(hit));
  mock_a11y_focus_manager()->set_should_get_a11y_focus_fail(true);

  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  // We need to inspect the focus that was set by us, so flip the mock behavior.
  mock_a11y_focus_manager()->set_should_get_a11y_focus_fail(false);

  auto focus = mock_a11y_focus_manager()->GetA11yFocus();
  ASSERT_TRUE(focus);
  EXPECT_EQ(focus->node_id, 0u);
  EXPECT_EQ(focus->view_ref_koid, mock_semantic_provider()->koid());

  EXPECT_FALSE(mock_speaker()->ReceivedSpeak());
  ASSERT_TRUE(mock_speaker()->speak_node_ids().empty());
}

TEST_F(ExploreActionTest, HitTestNodeIDResultIsNotPresentInTheTree) {
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  gesture_context.current_pointer_locations[0].local_point.x = 10;
  gesture_context.current_pointer_locations[0].local_point.y = 10;

  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(100u);
  mock_semantics_source()->SetHitTestResult(mock_semantic_provider()->koid(), std::move(hit));

  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  EXPECT_FALSE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_FALSE(mock_speaker()->ReceivedSpeak());
}

TEST_F(ExploreActionTest, HitTestNodeNotDescribable) {
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  gesture_context.current_pointer_locations[0].local_point.x = 10;
  gesture_context.current_pointer_locations[0].local_point.y = 10;

  // Set hit test result.
  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(1u);
  mock_semantics_source()->SetHitTestResult(mock_semantic_provider()->koid(), std::move(hit));

  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_FALSE(mock_speaker()->speak_node_ids().empty());
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 0u);
}

TEST_F(ExploreActionTest, IgnoresRedundantNodes) {
  Node root_node = CreateTestNode(0u, "root");
  root_node.set_child_ids({1u});

  Node node_1 = CreateTestNode(1u, "repeated node");
  node_1.set_child_ids({2u});

  Node node_2 = CreateTestNode(2u, "repeated node");
  node_2.set_child_ids({3u});

  Node node_3 = CreateTestNode(3u, "repeated node");

  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(root_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node_1));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node_2));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node_3));

  // Hit node_3.
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  gesture_context.current_pointer_locations[0].local_point.x = 10;
  gesture_context.current_pointer_locations[0].local_point.y = 10;

  // Set hit test result.
  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(3u);
  mock_semantics_source()->SetHitTestResult(mock_semantic_provider()->koid(), std::move(hit));

  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  // We walk up to node 1 because nodes 2 and 3 are rendundant.
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 1u);
}

TEST_F(ExploreActionTest, ContinuousExploreSpeaksNodeWhenA11yFocusIsDifferent) {
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  gesture_context.current_pointer_locations[0].local_point.x = kLocalCoordForTesting;
  gesture_context.current_pointer_locations[0].local_point.y = kLocalCoordForTesting;

  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(0u);
  mock_semantics_source()->SetHitTestResult(mock_semantic_provider()->koid(), std::move(hit));

  mock_screen_reader_context()->set_mode(
      a11y::ScreenReaderContext::ScreenReaderMode::kContinuousExploration);
  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  // Checks that a new a11y focus was set.
  EXPECT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());

  auto focus = mock_a11y_focus_manager()->GetA11yFocus();
  ASSERT_TRUE(focus);
  EXPECT_EQ(focus->node_id, 0u);
  EXPECT_EQ(focus->view_ref_koid, mock_semantic_provider()->koid());

  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
}

TEST_F(ExploreActionTest, ContinuousExploreDropsWhenA11yFocusIsTheSame) {
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  gesture_context.current_pointer_locations[0].local_point.x = kLocalCoordForTesting;
  gesture_context.current_pointer_locations[0].local_point.y = kLocalCoordForTesting;

  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(0u);
  mock_semantics_source()->SetHitTestResult(mock_semantic_provider()->koid(), std::move(hit));

  mock_screen_reader_context()->set_mode(
      a11y::ScreenReaderContext::ScreenReaderMode::kContinuousExploration);
  mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), 0, [](auto...) {});
  explore_action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_FALSE(mock_speaker()->ReceivedSpeak());
  ASSERT_TRUE(mock_speaker()->speak_node_ids().empty());
}

TEST_F(ExploreActionTest, ReadsKeyboardKey) {
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  gesture_context.current_pointer_locations[0].local_point.x = kLocalCoordForTesting;
  gesture_context.current_pointer_locations[0].local_point.y = kLocalCoordForTesting;

  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(0u);
  mock_semantics_source()->SetHitTestResult(mock_semantic_provider()->koid(), std::move(hit));

  mock_screen_reader_context()->set_virtual_keyboard_focused(true);

  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_speaker()->ReceivedSpeakLabel());
  ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 0u);
}

TEST_F(ExploreActionTest, UpdatesNavigationContext) {
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
  cell_attributes->set_row_index(1u);
  cell_attributes->set_column_index(2u);

  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(root_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(child_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(table_node));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(cell_node));

  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();

  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(3u);
  mock_semantics_source()->SetHitTestResult(mock_semantic_provider()->koid(), std::move(hit));

  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  const auto& navigation_context = mock_screen_reader_context()->current_navigation_context();
  EXPECT_EQ(navigation_context.containers.size(), 1u);
  EXPECT_EQ(navigation_context.containers[0].node_id, 2u);
}

TEST_F(ExploreActionTest, UserExitsTableInSeparateView) {
  // Create table in view 1.
  {
    Node root_node = CreateTestNode(0u, "root 1");
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
    cell_attributes->set_row_index(1u);
    cell_attributes->set_column_index(2u);

    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                                std::move(root_node));
    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                                std::move(child_node));
    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                                std::move(table_node));
    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                                std::move(cell_node));
  }

  // Create a root node in a separate view.
  MockSemanticProvider semantic_provider_2(nullptr, nullptr);
  {
    Node root_node = CreateTestNode(0u, "root 2");
    mock_semantics_source()->CreateSemanticNode(semantic_provider_2.koid(), std::move(root_node));
  }

  // Set the current navigation context to have node 2 as its container.
  a11y::ScreenReaderContext::NavigationContext navigation_context;
  navigation_context.containers = {{.node_id = 2u}};
  navigation_context.view_ref_koid = mock_semantic_provider()->koid();

  // Set the hit result to the root of view 2.
  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(0u);
  mock_semantics_source()->SetHitTestResult(semantic_provider_2.koid(), std::move(hit));

  // Run the action using a gesture context in view 2.
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = semantic_provider_2.koid();

  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  // Verify that previous_container was NOT set in the message context.
  ASSERT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_EQ(mock_a11y_focus_manager()->GetA11yFocus().value().node_id, 0u);
  EXPECT_EQ(mock_a11y_focus_manager()->GetA11yFocus().value().view_ref_koid,
            semantic_provider_2.koid());
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  EXPECT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 0u);
  ASSERT_TRUE(mock_speaker()->speak_node_message_contexts()[0].entered_containers.empty());
}

TEST_F(ExploreActionTest, NextActionEnteringAndExitingMultipleNestedContainers) {
  /*
    The test data is laid out like this:
    (0 - root
      (1 - table_1
        (2 - cell_1
          (3 - list_1
            (4 - nested_list_1
              (5 - statictext_1)))))
      (6 - table_2
        (7 - cell_2
          (8 - list_2
            (9 - nested_list2
              (10 - statictext_2)))))
    )

    In this test, we start at 'static_text_1' and take one NextAction which should navigate to
    'static_text_2'.

    Note: Technically, in practice, we would also usually have 'list_element' nodes in between the
    list and the static_text nodes. However, that's not needed for this test.
  */

  Node root = CreateTestNode(0u, "root");
  root.set_child_ids({1u, 6u});

  Node table_1 = CreateTestNode(1u, "table_1");
  table_1.set_child_ids({2u});
  Node cell_1 = CreateTestNode(2u, "cell_1");
  cell_1.set_child_ids({3u});
  Node list_1 = CreateTestNode(3u, "list2");
  cell_1.set_child_ids({4u});
  Node nested_list_1 = CreateTestNode(4u, "nested_list_1");
  nested_list_1.set_child_ids({5u});
  Node static_text_1 = CreateTestNode(5u, "static_text_1");

  Node table_2 = CreateTestNode(6u, "table_2");
  table_2.set_child_ids({7u});
  Node cell_2 = CreateTestNode(7u, "cell_2");
  cell_2.set_child_ids({8u});
  Node list_2 = CreateTestNode(8u, "nested_list_2");
  list_2.set_child_ids({9u});
  Node nested_list_2 = CreateTestNode(9u, "nested_list_2");
  nested_list_2.set_child_ids({10u});
  Node static_text_2 = CreateTestNode(10u, "static_text_2");

  table_1.set_role(fuchsia::accessibility::semantics::Role::TABLE)
      .mutable_attributes()
      ->mutable_table_attributes()
      ->set_number_of_rows(1u)
      .set_number_of_columns(1u);
  table_2.set_role(fuchsia::accessibility::semantics::Role::TABLE)
      .mutable_attributes()
      ->mutable_table_attributes()
      ->set_number_of_rows(1u)
      .set_number_of_columns(1u);

  cell_1.set_role(fuchsia::accessibility::semantics::Role::CELL)
      .mutable_attributes()
      ->mutable_table_cell_attributes()
      ->set_row_index(0u)
      .set_column_index(0u);
  cell_2.set_role(fuchsia::accessibility::semantics::Role::CELL)
      .mutable_attributes()
      ->mutable_table_cell_attributes()
      ->set_row_index(0u)
      .set_column_index(0u);

  list_1.set_role(fuchsia::accessibility::semantics::Role::LIST);
  list_2.set_role(fuchsia::accessibility::semantics::Role::LIST);
  nested_list_1.set_role(fuchsia::accessibility::semantics::Role::LIST);
  nested_list_2.set_role(fuchsia::accessibility::semantics::Role::LIST);

  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(root));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(table_1));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(cell_1));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(list_1));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(nested_list_1));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(static_text_1));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(table_2));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(cell_2));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(list_2));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(nested_list_2));
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(),
                                              std::move(static_text_2));

  // Start at static_text_1.
  mock_a11y_focus_manager()->UpdateA11yFocus(mock_semantic_provider()->koid(), 5u);
  a11y::ScreenReaderContext::NavigationContext navigation_context;
  navigation_context.containers = {
      {.node_id = 1u, .table_context = {{.row_index = 0, .column_index = 0}}},
      {.node_id = 3u, .table_context = {}},
      {.node_id = 4u, .table_context = {}}};
  navigation_context.view_ref_koid = mock_semantic_provider()->koid();
  mock_screen_reader_context()->set_current_navigation_context(navigation_context);

  // Navigate from static_text_1 -> static_text_2, exiting 2 lists and 1 table, and entering 2 lists
  // and 1 table.
  a11y::ExploreAction explore_action(action_context(), mock_screen_reader_context());

  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  gesture_context.current_pointer_locations[0].local_point.x = kLocalCoordForTesting;
  gesture_context.current_pointer_locations[0].local_point.y = kLocalCoordForTesting;

  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(10u);
  mock_semantics_source()->SetHitTestResult(mock_semantic_provider()->koid(), std::move(hit));
  EXPECT_FALSE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());

  explore_action.Run(gesture_context);
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_a11y_focus_manager()->IsSetA11yFocusCalled());
  EXPECT_EQ(mock_a11y_focus_manager()->GetA11yFocus().value().node_id, 10u);
  EXPECT_EQ(mock_semantic_provider()->koid(),
            mock_a11y_focus_manager()->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_speaker()->ReceivedSpeak());
  ASSERT_EQ(mock_speaker()->speak_node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker()->speak_node_ids()[0], 10u);

  ASSERT_EQ(mock_speaker()->speak_node_message_contexts().size(), 1u);
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts()[0].exited_containers.size(), 3u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].exited_containers[0]->node_id(), 4u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].exited_containers[1]->node_id(), 3u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].exited_containers[2]->node_id(), 1u);
  ASSERT_EQ(mock_speaker()->speak_node_message_contexts()[0].entered_containers.size(), 3u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].entered_containers[0]->node_id(), 6u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].entered_containers[1]->node_id(), 8u);
  EXPECT_EQ(mock_speaker()->speak_node_message_contexts()[0].entered_containers[2]->node_id(), 9u);
}

}  // namespace
}  // namespace accessibility_test
