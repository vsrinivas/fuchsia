// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/change_semantic_level_action.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <zircon/types.h>

#include <memory>

#include <gmock/gmock.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/screen_reader_action_test_fixture.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"

namespace accessibility_test {
namespace {

using a11y::ScreenReaderContext;
using fuchsia::intl::l10n::MessageIds;
using testing::ElementsAre;

class ChangeSemanticLevelAction : public ScreenReaderActionTest {
 public:
  ChangeSemanticLevelAction() = default;
  ~ChangeSemanticLevelAction() override = default;

  void SetUp() override {
    ScreenReaderActionTest::SetUp();

    fuchsia::accessibility::semantics::Node node = CreateTestNode(0u, "Label A");
    node.mutable_states()->set_range_value(42.0);
    mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node));

    mock_a11y_focus_manager()->SetA11yFocus(mock_semantic_provider()->koid(), 0u,
                                            [](bool result) { EXPECT_TRUE(result); });
  }
};

TEST_F(ChangeSemanticLevelAction, NoChangeForNonSliderNode) {
  // The focus is not important when it is not a slider node.
  mock_a11y_focus_manager()->set_should_get_a11y_focus_fail(true);
  a11y::ChangeSemanticLevelAction action(a11y::ChangeSemanticLevelAction::Direction::kForward,
                                         action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kDefault);
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kDefault);
  EXPECT_THAT(mock_speaker()->message_ids(),
              ElementsAre(MessageIds::DEFAULT_NAVIGATION_GRANULARITY,
                          MessageIds::DEFAULT_NAVIGATION_GRANULARITY));
}

// TODO(fxb/63293): Enable when word and character navigation exist.
TEST_F(ChangeSemanticLevelAction, DISABLED_CyclesForwardThroughLevelsForNonSliderNode) {
  // The focus is not important when it is not a slider node.
  mock_a11y_focus_manager()->set_should_get_a11y_focus_fail(true);
  a11y::ChangeSemanticLevelAction action(a11y::ChangeSemanticLevelAction::Direction::kForward,
                                         action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kCharacter);
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kWord);
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kDefault);
  EXPECT_THAT(mock_speaker()->message_ids(),
              ElementsAre(MessageIds::CHARACTER_GRANULARITY, MessageIds::WORD_GRANULARITY,
                          MessageIds::DEFAULT_NAVIGATION_GRANULARITY));
}

// TODO(fxb/63293): Enable when word and character navigation exist.
TEST_F(ChangeSemanticLevelAction, DISABLED_CyclesBackwardThroughLevelsForNonSliderNode) {
  // The focus is not important when it is not a slider node.
  mock_a11y_focus_manager()->set_should_get_a11y_focus_fail(true);
  a11y::ChangeSemanticLevelAction action(a11y::ChangeSemanticLevelAction::Direction::kBackward,
                                         action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kWord);
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kCharacter);
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kDefault);
  EXPECT_THAT(mock_speaker()->message_ids(),
              ElementsAre(MessageIds::WORD_GRANULARITY, MessageIds::CHARACTER_GRANULARITY,
                          MessageIds::DEFAULT_NAVIGATION_GRANULARITY));
}

TEST_F(ChangeSemanticLevelAction, CyclesForwardThroughLevelsForSliderNode) {
  a11y::ChangeSemanticLevelAction action(a11y::ChangeSemanticLevelAction::Direction::kForward,
                                         action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kAdjustValue);
  /* TODO(fxb/63293): Uncomment when word and character navigation exist.
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kCharacter);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
  ScreenReaderContext::SemanticLevel::kWord);
  */
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kDefault);
  EXPECT_THAT(mock_speaker()->message_ids(),
              ElementsAre(MessageIds::ADJUST_VALUE_GRANULARITY,
                          MessageIds::DEFAULT_NAVIGATION_GRANULARITY));
}

TEST_F(ChangeSemanticLevelAction, CyclesBackwardThroughLevelsForSliderNode) {
  a11y::ChangeSemanticLevelAction action(a11y::ChangeSemanticLevelAction::Direction::kBackward,
                                         action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  /* TODO(fxb/63293): Uncomment when word and character navigation exist.
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
  ScreenReaderContext::SemanticLevel::kWord); action.Run(action_data); RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kCharacter);
  */
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kAdjustValue);
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kDefault);
  EXPECT_THAT(mock_speaker()->message_ids(),
              ElementsAre(MessageIds::ADJUST_VALUE_GRANULARITY,
                          MessageIds::DEFAULT_NAVIGATION_GRANULARITY));
}

TEST_F(ChangeSemanticLevelAction, CyclesForwardThroughLevelsForSliderNodeNoRangeValue) {
  // Overwrite the test node with a node that does NOT have a range value, but
  // DOES have role SLIDER.
  fuchsia::accessibility::semantics::Node node = CreateTestNode(0u, "Label A");
  node.set_role(fuchsia::accessibility::semantics::Role::SLIDER);
  mock_semantics_source()->CreateSemanticNode(mock_semantic_provider()->koid(), std::move(node));
  a11y::ChangeSemanticLevelAction action(a11y::ChangeSemanticLevelAction::Direction::kForward,
                                         action_context(), mock_screen_reader_context());
  a11y::GestureContext gesture_context;
  gesture_context.view_ref_koid = mock_semantic_provider()->koid();
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kAdjustValue);
  /* TODO(fxb/63293): Uncomment when word and character navigation exist.
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kCharacter);
  action.Run(action_data);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
  ScreenReaderContext::SemanticLevel::kWord);
  */
  action.Run(gesture_context);
  RunLoopUntilIdle();
  EXPECT_EQ(mock_screen_reader_context()->semantic_level(),
            ScreenReaderContext::SemanticLevel::kDefault);
  EXPECT_THAT(mock_speaker()->message_ids(),
              ElementsAre(MessageIds::ADJUST_VALUE_GRANULARITY,
                          MessageIds::DEFAULT_NAVIGATION_GRANULARITY));
}

}  // namespace
}  // namespace accessibility_test
