// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/change_semantic_level_action.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <zircon/types.h>

#include <memory>

#include <gmock/gmock.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_registry.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_event_manager.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {
namespace {

using a11y::ScreenReaderContext;
using fuchsia::intl::l10n::MessageIds;
using testing::ElementsAre;

class ChangeSemanticLevelAction : public gtest::TestLoopFixture {
 public:
  ChangeSemanticLevelAction()
      : view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                      std::make_unique<MockViewSemanticsFactory>(),
                      std::make_unique<MockAnnotationViewFactory>(),
                      std::make_unique<MockSemanticsEventManager>(), context_provider_.context(),
                      context_provider_.context()->outgoing()->debug_dir()),
        semantic_provider_(&view_manager_) {
    action_context_.semantics_source = &view_manager_;

    screen_reader_context_ = std::make_unique<MockScreenReaderContext>();
    a11y_focus_manager_ptr_ = screen_reader_context_->mock_a11y_focus_manager_ptr();
    mock_speaker_ptr_ = screen_reader_context_->mock_speaker_ptr();
    view_manager_.SetSemanticsEnabled(true);
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0);
    node.set_role(fuchsia::accessibility::semantics::Role::SLIDER);
    std::vector<decltype(node)> update_nodes;
    update_nodes.push_back(std::move(node));
    semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();
    semantic_provider_.CommitUpdates();
    RunLoopUntilIdle();
    a11y_focus_manager_ptr_->SetA11yFocus(semantic_provider_.koid(), 0,
                                          [](bool result) { EXPECT_TRUE(result); });
  }

  vfs::PseudoDir* debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

  sys::testing::ComponentContextProvider context_provider_;
  a11y::ViewManager view_manager_;
  a11y::ScreenReaderAction::ActionContext action_context_;
  std::unique_ptr<MockScreenReaderContext> screen_reader_context_;
  MockA11yFocusManager* a11y_focus_manager_ptr_;
  MockScreenReaderContext::MockSpeaker* mock_speaker_ptr_;
  MockSemanticProvider semantic_provider_;
};

TEST_F(ChangeSemanticLevelAction, CyclesForwardThroughLevelsForNonSliderNode) {
  // The focus is not important when it is not a slider node.
  a11y_focus_manager_ptr_->set_should_get_a11y_focus_fail(true);
  a11y::ChangeSemanticLevelAction action(a11y::ChangeSemanticLevelAction::Direction::kForward,
                                         &action_context_, screen_reader_context_.get());
  a11y::ScreenReaderAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            ScreenReaderContext::SemanticLevel::kCharacter);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(), ScreenReaderContext::SemanticLevel::kWord);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            ScreenReaderContext::SemanticLevel::kNormalNavigation);
  EXPECT_THAT(mock_speaker_ptr_->message_ids(),
              ElementsAre(MessageIds::CHARACTER_GRANULARITY, MessageIds::WORD_GRANULARITY,
                          MessageIds::NORMAL_NAVIGATION_GRANULARITY));
}

TEST_F(ChangeSemanticLevelAction, CyclesBackwardThroughLevelsForNonSliderNode) {
  // The focus is not important when it is not a slider node.
  a11y_focus_manager_ptr_->set_should_get_a11y_focus_fail(true);
  a11y::ChangeSemanticLevelAction action(a11y::ChangeSemanticLevelAction::Direction::kBackward,
                                         &action_context_, screen_reader_context_.get());
  a11y::ScreenReaderAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(), ScreenReaderContext::SemanticLevel::kWord);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            ScreenReaderContext::SemanticLevel::kCharacter);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            ScreenReaderContext::SemanticLevel::kNormalNavigation);
  EXPECT_THAT(mock_speaker_ptr_->message_ids(),
              ElementsAre(MessageIds::WORD_GRANULARITY, MessageIds::CHARACTER_GRANULARITY,
                          MessageIds::NORMAL_NAVIGATION_GRANULARITY));
}

TEST_F(ChangeSemanticLevelAction, CyclesForwardThroughLevelsForSliderNode) {
  a11y::ChangeSemanticLevelAction action(a11y::ChangeSemanticLevelAction::Direction::kForward,
                                         &action_context_, screen_reader_context_.get());
  a11y::ScreenReaderAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            ScreenReaderContext::SemanticLevel::kAdjustValue);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            ScreenReaderContext::SemanticLevel::kCharacter);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(), ScreenReaderContext::SemanticLevel::kWord);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            ScreenReaderContext::SemanticLevel::kNormalNavigation);
  EXPECT_THAT(mock_speaker_ptr_->message_ids(),
              ElementsAre(MessageIds::ADJUST_VALUE_GRANULARITY, MessageIds::CHARACTER_GRANULARITY,
                          MessageIds::WORD_GRANULARITY, MessageIds::NORMAL_NAVIGATION_GRANULARITY));
}

TEST_F(ChangeSemanticLevelAction, CyclesBackwardThroughLevelsForSliderNode) {
  a11y::ChangeSemanticLevelAction action(a11y::ChangeSemanticLevelAction::Direction::kBackward,
                                         &action_context_, screen_reader_context_.get());
  a11y::ScreenReaderAction::ActionData action_data;
  action_data.current_view_koid = semantic_provider_.koid();
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(), ScreenReaderContext::SemanticLevel::kWord);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            ScreenReaderContext::SemanticLevel::kCharacter);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            ScreenReaderContext::SemanticLevel::kAdjustValue);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            ScreenReaderContext::SemanticLevel::kNormalNavigation);
  EXPECT_THAT(
      mock_speaker_ptr_->message_ids(),
      ElementsAre(MessageIds::WORD_GRANULARITY, MessageIds::CHARACTER_GRANULARITY,
                  MessageIds::ADJUST_VALUE_GRANULARITY, MessageIds::NORMAL_NAVIGATION_GRANULARITY));
}

}  // namespace
}  // namespace accessibility_test
