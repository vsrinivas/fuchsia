// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_registry.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_drag_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_tts_engine.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/view_manager.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::SemanticsManager;
using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using PointerEventPhase = fuchsia::ui::input::PointerEventPhase;

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Hit;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;

class ScreenReaderTest : public gtest::TestLoopFixture {
 public:
  ScreenReaderTest()
      : tts_manager_(context_provider_.context()),
        view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                      context_provider_.context()->outgoing()->debug_dir()),
        a11y_focus_manager_(std::make_unique<MockA11yFocusManager>()),
        a11y_focus_manager_ptr_(a11y_focus_manager_.get()),
        context_(std::make_unique<a11y::ScreenReaderContext>(std::move(a11y_focus_manager_))),
        context_ptr_(context_.get()),
        screen_reader_(std::move(context_), &view_manager_, &tts_manager_),
        semantic_provider_(&view_manager_) {
    screen_reader_.BindGestures(gesture_manager_.gesture_handler());
    // Initialize Mock TTS Engine.
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
    Node clone_node;
    node.Clone(&clone_node);
    update_nodes.push_back(std::move(clone_node));

    // Update the node created above.
    semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();

    // Commit nodes.
    semantic_provider_.CommitUpdates();
    RunLoopUntilIdle();
  }

  void SendPointerEvents(const std::vector<PointerParams> &events) {
    for (const auto &event : events) {
      gesture_manager_.OnEvent(
          ToPointerEvent(event, 0 /*event time (unused)*/, semantic_provider_.koid()));
    }
  }

  void CreateOnOneFingerTapAction() {
    SendPointerEvents(
        TapEvents(1, {0, 0} /*global coordinates of tap ignored by mock semantic provider*/));
  }

  // Create a test node with only a node id and a label.
  Node CreateTestNode(uint32_t node_id, std::string label) {
    Node node = Node();
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

  sys::testing::ComponentContextProvider context_provider_;
  a11y::TtsManager tts_manager_;
  a11y::ViewManager view_manager_;
  a11y::GestureManager gesture_manager_;

  std::unique_ptr<MockA11yFocusManager> a11y_focus_manager_;
  MockA11yFocusManager *a11y_focus_manager_ptr_;
  std::unique_ptr<a11y::ScreenReaderContext> context_;
  a11y::ScreenReaderContext *context_ptr_;
  a11y::ScreenReader screen_reader_;
  accessibility_test::MockSemanticProvider semantic_provider_;
  accessibility_test::MockTtsEngine mock_tts_engine_;
};

TEST_F(ScreenReaderTest, OnOneFingerSingleTapAction) {
  semantic_provider_.SetHitTestResult(0);

  // Create OnOneFingerTap Action.
  CreateOnOneFingerTapAction();
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  // Verify that TTS is called when OneFingerTapAction was performed.
  EXPECT_TRUE(mock_tts_engine_.ReceivedSpeak());
  // Check if Utterance and Speak functions are called in Tts.
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), "Label A");
}

TEST_F(ScreenReaderTest, OnOneFingerDoubleTapAction) {
  // Prepare the context of the screen reader(by setting A11yFocusInfo), assuming that it has a node
  // selected in a particular view.
  a11y_focus_manager_ptr_->SetA11yFocus(semantic_provider_.koid(), 0,
                                        [](bool result) { EXPECT_TRUE(result); });

  semantic_provider_.SetRequestedAction(fuchsia::accessibility::semantics::Action::SET_FOCUS);

  // Create OnOneFingerDoubleTap Action.
  CreateOnOneFingerTapAction();
  CreateOnOneFingerTapAction();
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  EXPECT_EQ(fuchsia::accessibility::semantics::Action::DEFAULT,
            semantic_provider_.GetRequestedAction());
}

TEST_F(ScreenReaderTest, OnOneFingerDragAction) {
  semantic_provider_.SetHitTestResult(0);

  // Create one finger drag action.
  glm::vec2 initial_update_ndc_position = {0, .7f};
  glm::vec2 final_update_ndc_position = {0, 1.0f};

  SendPointerEvents(DownEvents(1, initial_update_ndc_position) +
                    MoveEvents(1, initial_update_ndc_position, {0, .8f}));
  // At this point, the drag hasn't started yet, so Screen Reader is not in continuous exploration.
  EXPECT_EQ(context_ptr_->mode(), a11y::ScreenReaderContext::ScreenReaderMode::kNormal);
  // Wait for the drag delay to elapse, at which point the recognizer should claim the win and
  // invoke the update callback.
  RunLoopFor(a11y::OneFingerDragRecognizer::kDefaultMinDragDuration);
  // The drag has started, so continuous exploration mode.
  EXPECT_EQ(context_ptr_->mode(),
            a11y::ScreenReaderContext::ScreenReaderMode::kContinuousExploration);

  SendPointerEvents(MoveEvents(1, {0, 0.8f}, final_update_ndc_position, 5));
  // Dragging still in progress.
  EXPECT_EQ(context_ptr_->mode(),
            a11y::ScreenReaderContext::ScreenReaderMode::kContinuousExploration);
  SendPointerEvents(UpEvents(1, final_update_ndc_position));
  RunLoopUntilIdle();
  // The drag has ended, so continuous exploration.
  EXPECT_EQ(context_ptr_->mode(), a11y::ScreenReaderContext::ScreenReaderMode::kNormal);
  // Verify that TTS is called when ExploreAction associated with the drag gesture was performed.
  // Note that because the mock is always returning the same hit test result (node 0), and
  // continuous exploration is on, only one spoken utterance is expected.
  EXPECT_TRUE(mock_tts_engine_.ReceivedSpeak());
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), "Label A");
}

}  // namespace
}  // namespace accessibility_test
