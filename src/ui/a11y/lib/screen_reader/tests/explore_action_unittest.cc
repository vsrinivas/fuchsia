// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/explore_action.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_event_manager.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;

// Arbitrary number to set a local coordinate when sending for hit testing.
static const int kLocalCoordForTesting = 10;

class ExploreActionTest : public gtest::TestLoopFixture {
 public:
  ExploreActionTest()
      : context_provider_(),
        view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                      std::make_unique<MockViewSemanticsFactory>(),
                      std::make_unique<MockAnnotationViewFactory>(),
                      std::make_unique<MockSemanticsEventManager>(), context_provider_.context(),
                      context_provider_.context()->outgoing()->debug_dir()),
        semantic_provider_(&view_manager_) {
    action_context_.semantics_source = &view_manager_;
    view_manager_.SetSemanticsEnabled(true);

    screen_reader_context_ = std::make_unique<MockScreenReaderContext>();
    a11y_focus_manager_ptr_ = screen_reader_context_->mock_a11y_focus_manager_ptr();
    mock_speaker_ptr_ = screen_reader_context_->mock_speaker_ptr();

    a11y_focus_manager_ptr_->SetA11yFocus(100u, 10000, [](bool result) { EXPECT_TRUE(result); });
    a11y_focus_manager_ptr_->ResetExpectations();
    // Creating test node to update.
    std::vector<Node> update_nodes;
    Node node = accessibility_test::CreateTestNode(0, "Label A", {1u});
    Node node_2 = accessibility_test::CreateTestNode(1u, "", {});
    update_nodes.push_back(std::move(node));
    update_nodes.push_back(std::move(node_2));

    // Update the node created above.
    semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();

    // Commit nodes.
    semantic_provider_.CommitUpdates();
    RunLoopUntilIdle();
  }

  ~ExploreActionTest() override = default;

  vfs::PseudoDir* debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }
  sys::testing::ComponentContextProvider context_provider_;
  a11y::ViewManager view_manager_;
  a11y::ScreenReaderAction::ActionContext action_context_;
  MockSemanticProvider semantic_provider_;
  std::unique_ptr<MockScreenReaderContext> screen_reader_context_;
  MockA11yFocusManager* a11y_focus_manager_ptr_;
  MockScreenReaderContext::MockSpeaker* mock_speaker_ptr_;
};

TEST_F(ExploreActionTest, SuccessfulExploreActionReadsNode) {
  a11y::ExploreAction explore_action(&action_context_, screen_reader_context_.get());
  a11y::ExploreAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  action_data.local_point.x = kLocalCoordForTesting;
  action_data.local_point.y = kLocalCoordForTesting;

  semantic_provider_.SetHitTestResult(0);
  EXPECT_FALSE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());

  explore_action.Run(action_data);
  RunLoopUntilIdle();

  // Checks that a new a11y focus was set.
  EXPECT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  auto focus = a11y_focus_manager_ptr_->GetA11yFocus();
  ASSERT_TRUE(focus);
  EXPECT_EQ(focus->node_id, 0u);
  EXPECT_EQ(focus->view_ref_koid, semantic_provider_.koid());
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->node_ids()[0], 0u);
}

TEST_F(ExploreActionTest, HitTestFails) {
  a11y::ExploreAction explore_action(&action_context_, screen_reader_context_.get());
  a11y::ExploreAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  action_data.local_point.x = 10;
  action_data.local_point.y = 10;

  // No node will be hit.
  semantic_provider_.SetHitTestResult(std::nullopt);

  explore_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_FALSE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_FALSE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_TRUE(mock_speaker_ptr_->node_ids().empty());
}

TEST_F(ExploreActionTest, SetA11yFocusFails) {
  a11y::ExploreAction explore_action(&action_context_, screen_reader_context_.get());
  a11y::ExploreAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  action_data.local_point.x = 10;
  action_data.local_point.y = 10;

  semantic_provider_.SetHitTestResult(0);
  a11y_focus_manager_ptr_->set_should_set_a11y_focus_fail(true);

  explore_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  auto focus = a11y_focus_manager_ptr_->GetA11yFocus();
  ASSERT_TRUE(focus);
  EXPECT_NE(focus->node_id, 0u);
  EXPECT_NE(focus->view_ref_koid, semantic_provider_.koid());

  EXPECT_FALSE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_TRUE(mock_speaker_ptr_->node_ids().empty());
}

TEST_F(ExploreActionTest, GettingA11yFocusFails) {
  a11y::ExploreAction explore_action(&action_context_, screen_reader_context_.get());
  a11y::ExploreAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  action_data.local_point.x = 10;
  action_data.local_point.y = 10;

  semantic_provider_.SetHitTestResult(0);
  a11y_focus_manager_ptr_->set_should_get_a11y_focus_fail(true);

  explore_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  // We need to inspect the focus that was set by us, so flip the mock behavior.
  a11y_focus_manager_ptr_->set_should_get_a11y_focus_fail(false);

  auto focus = a11y_focus_manager_ptr_->GetA11yFocus();
  ASSERT_TRUE(focus);
  EXPECT_EQ(focus->node_id, 0u);
  EXPECT_EQ(focus->view_ref_koid, semantic_provider_.koid());

  EXPECT_FALSE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_TRUE(mock_speaker_ptr_->node_ids().empty());
}

TEST_F(ExploreActionTest, HitTestNodeIDResultIsNotPresentInTheTree) {
  a11y::ExploreAction explore_action(&action_context_, screen_reader_context_.get());
  a11y::ExploreAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  action_data.local_point.x = 10;
  action_data.local_point.y = 10;

  semantic_provider_.SetHitTestResult(100);

  explore_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_FALSE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_FALSE(mock_speaker_ptr_->ReceivedSpeak());
}

TEST_F(ExploreActionTest, HitTestNodeNotDescribable) {
  a11y::ExploreAction explore_action(&action_context_, screen_reader_context_.get());
  a11y::ExploreAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  action_data.local_point.x = 10;
  action_data.local_point.y = 10;

  semantic_provider_.SetHitTestResult(1u);

  explore_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_FALSE(mock_speaker_ptr_->node_ids().empty());
  EXPECT_EQ(mock_speaker_ptr_->node_ids()[0], 0u);
}

TEST_F(ExploreActionTest, ContinuousExploreSpeaksNodeWhenA11yFocusIsDifferent) {
  a11y::ExploreAction explore_action(&action_context_, screen_reader_context_.get());
  a11y::ExploreAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  action_data.local_point.x = kLocalCoordForTesting;
  action_data.local_point.y = kLocalCoordForTesting;

  semantic_provider_.SetHitTestResult(0);

  screen_reader_context_->set_mode(
      a11y::ScreenReaderContext::ScreenReaderMode::kContinuousExploration);
  explore_action.Run(action_data);
  RunLoopUntilIdle();

  // Checks that a new a11y focus was set.
  EXPECT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());

  auto focus = a11y_focus_manager_ptr_->GetA11yFocus();
  ASSERT_TRUE(focus);
  EXPECT_EQ(focus->node_id, 0u);
  EXPECT_EQ(focus->view_ref_koid, semantic_provider_.koid());

  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
}

TEST_F(ExploreActionTest, ContinuousExploreDropsWhenA11yFocusIsTheSame) {
  a11y::ExploreAction explore_action(&action_context_, screen_reader_context_.get());
  a11y::ExploreAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  action_data.local_point.x = kLocalCoordForTesting;
  action_data.local_point.y = kLocalCoordForTesting;

  semantic_provider_.SetHitTestResult(0);

  screen_reader_context_->set_mode(
      a11y::ScreenReaderContext::ScreenReaderMode::kContinuousExploration);
  a11y_focus_manager_ptr_->SetA11yFocus(semantic_provider_.koid(), 0, [](auto...) {});
  explore_action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_FALSE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_TRUE(mock_speaker_ptr_->node_ids().empty());
}

}  // namespace
}  // namespace accessibility_test
