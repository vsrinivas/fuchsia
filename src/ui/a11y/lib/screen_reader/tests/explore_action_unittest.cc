// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/explore_action.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <src/ui/a11y/lib/screen_reader/tests/mocks/mock_tts_engine.h>
#include <src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accesibility_test {
namespace {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;

// Arbitrary number to set a local coordinate when sending for hit testing.
static const int kLocalCoordForTesting = 10;

class ExploreActionTest : public gtest::TestLoopFixture {
 public:
  ExploreActionTest()
      : view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                      context_provider_.context()->outgoing()->debug_dir()),
        tts_manager_(context_provider_.context()),
        semantic_provider_(&view_manager_) {
    action_context_.view_manager = &view_manager_;
    view_manager_.SetSemanticsEnabled(true);

    tts_manager_.OpenEngine(action_context_.tts_engine_ptr.NewRequest(),
                            [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                              EXPECT_TRUE(result.is_response());
                            });
    auto a11y_focus_manager = std::make_unique<accessibility_test::MockA11yFocusManager>();
    a11y_focus_manager_ptr_ = a11y_focus_manager.get();
    screen_reader_context_ =
        std::make_unique<a11y::ScreenReaderContext>(std::move(a11y_focus_manager));
    // Setup tts and basic semantic support for this test.
    fidl::InterfaceHandle<fuchsia::accessibility::tts::Engine> engine_handle =
        mock_tts_engine_.GetHandle();
    tts_manager_.RegisterEngine(
        std::move(engine_handle),
        [](fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result) {
          EXPECT_TRUE(result.is_response());
        });
    RunLoopUntilIdle();

    // Creating test node to update.
    std::vector<Node> update_nodes;
    Node node = CreateTestNode(0, "Label A");
    update_nodes.push_back(std::move(node));

    // Update the node created above.
    semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();

    // Commit nodes.
    semantic_provider_.CommitUpdates();
    RunLoopUntilIdle();
  }

  ~ExploreActionTest() = default;

  // Create a test node with only a node id and a label.
  Node CreateTestNode(uint32_t node_id, std::string label) {
    Node node;
    node.set_node_id(node_id);
    node.set_child_ids({});
    node.set_role(Role::UNKNOWN);
    node.set_attributes(Attributes());
    node.mutable_attributes()->set_label(std::move(label));
    fuchsia::ui::gfx::BoundingBox box;
    node.set_location(std::move(box));
    fuchsia::ui::gfx::mat4 transform;
    node.set_transform(std::move(transform));
    return node;
  }

  vfs::PseudoDir* debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

  sys::testing::ComponentContextProvider context_provider_;
  a11y::ViewManager view_manager_;
  a11y::ScreenReaderAction::ActionContext action_context_;
  a11y::TtsManager tts_manager_;
  accessibility_test::MockSemanticProvider semantic_provider_;
  accessibility_test::MockTtsEngine mock_tts_engine_;

  std::unique_ptr<a11y::ScreenReaderContext> screen_reader_context_;
  accessibility_test::MockA11yFocusManager* a11y_focus_manager_ptr_;
};

TEST_F(ExploreActionTest, SuccessfulExploreActionReadsLabel) {
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

  EXPECT_TRUE(mock_tts_engine_.ReceivedSpeak());

  // Check if Utterance and Speak functions are called in Tts.
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), "Label A");
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
  EXPECT_TRUE(mock_tts_engine_.ExamineUtterances().empty());
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
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

  EXPECT_TRUE(mock_tts_engine_.ExamineUtterances().empty());
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
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

  EXPECT_TRUE(mock_tts_engine_.ExamineUtterances().empty());
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
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

  EXPECT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_TRUE(mock_tts_engine_.ExamineUtterances().empty());
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
}

TEST_F(ExploreActionTest, NodeIsMissingLabelToBuildUtterance) {
  a11y::ExploreAction explore_action(&action_context_, screen_reader_context_.get());
  a11y::ExploreAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  action_data.local_point.x = 10;
  action_data.local_point.y = 10;

  semantic_provider_.SetHitTestResult(2);
  Node node;
  node.set_node_id(2);
  // Do not set label, and update the node.
  std::vector<Node> updates;
  updates.push_back(std::move(node));
  semantic_provider_.UpdateSemanticNodes(std::move(updates));
  semantic_provider_.CommitUpdates();
  RunLoopUntilIdle();

  explore_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_TRUE(mock_tts_engine_.ExamineUtterances().empty());
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
}

TEST_F(ExploreActionTest, EnqueueTtsUtteranceFails) {
  a11y::ExploreAction explore_action(&action_context_, screen_reader_context_.get());
  a11y::ExploreAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  action_data.local_point.x = 10;
  action_data.local_point.y = 10;

  semantic_provider_.SetHitTestResult(0);
  mock_tts_engine_.set_should_fail_enqueue(true);
  explore_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_TRUE(mock_tts_engine_.ExamineUtterances().empty());
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
}

TEST_F(ExploreActionTest, SpeakFails) {
  a11y::ExploreAction explore_action(&action_context_, screen_reader_context_.get());
  a11y::ExploreAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  // Note that x and y are set just for completeness of the data type. the semantic provider is
  // responsible for returning what was the hit based on these numbers.
  action_data.local_point.x = 10;
  action_data.local_point.y = 10;

  semantic_provider_.SetHitTestResult(0);
  mock_tts_engine_.set_should_fail_speak(true);
  explore_action.Run(action_data);
  RunLoopUntilIdle();

  EXPECT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  // It received the utterances, but failed Speak() later.
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), "Label A");

  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
}

}  // namespace
}  // namespace accesibility_test
