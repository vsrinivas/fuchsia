// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include "fuchsia/accessibility/gesture/cpp/fidl.h"
#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_registry.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_listener_registry.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_drag_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/swipe_recognizer_base.h"
#include "src/ui/a11y/lib/gesture_manager/tests/mocks/mock_gesture_listener.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree_service_factory.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"
#include "src/ui/a11y/lib/view/view_manager.h"

namespace accessibility_test {
namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using PointerEventPhase = fuchsia::ui::input::PointerEventPhase;
using fuchsia::accessibility::gesture::Type;
using fuchsia::accessibility::semantics::Node;
using Phase = fuchsia::ui::input::PointerEventPhase;

constexpr char kRootNodeLabel[] = "Label A";
constexpr char kChildNodeLabel[] = "Label B";
constexpr char kListenerUtterance[] = "Gesture Performed";
constexpr uint32_t kRootNodeId = 0;
constexpr uint32_t kChildNodeId = 1;
constexpr accessibility_test::PointerId kPointerId = 1;

class ScreenReaderTest : public gtest::TestLoopFixture {
 public:
  ScreenReaderTest()
      : factory_(std::make_unique<MockSemanticTreeServiceFactory>()),
        factory_ptr_(factory_.get()),
        context_provider_(),
        view_manager_(std::move(factory_), std::make_unique<MockViewSemanticsFactory>(),
                      std::make_unique<MockAnnotationViewFactory>(), context_provider_.context(),
                      context_provider_.context()->outgoing()->debug_dir()),
        context_(std::make_unique<MockScreenReaderContext>()),
        context_ptr_(context_.get()),
        a11y_focus_manager_ptr_(context_ptr_->mock_a11y_focus_manager_ptr()),
        mock_speaker_ptr_(context_ptr_->mock_speaker_ptr()),
        screen_reader_(std::move(context_), &view_manager_, &gesture_listener_registry_),
        semantic_provider_(&view_manager_) {
    screen_reader_.BindGestures(gesture_manager_.gesture_handler());
    gesture_listener_registry_.Register(mock_gesture_listener_.NewBinding(), []() {});

    semantic_provider_.SetSemanticsEnabled(true);
    view_manager_.SetSemanticsEnabled(true);
    factory_ptr_->service()->EnableSemanticsUpdates(true);
    AddNodeToSemanticTree();
  }

  void SendPointerEvents(const std::vector<PointerParams>& events, uint32_t fingers = 1) {
    for (const auto& event : events) {
      for (uint32_t finger = 0; finger < fingers; finger++) {
        auto pointer_event =
            ToPointerEvent(event, 0 /*event time (unused)*/, semantic_provider_.koid());
        pointer_event.set_pointer_id(finger);
        gesture_manager_.OnEvent(std::move(pointer_event));
      }
    }
  }

  void CreateOnOneFingerTapAction() {
    SendPointerEvents(TapEvents(
        kPointerId, {0, 0} /*global coordinates of tap ignored by mock semantic provider*/));
  }

  void AddNodeToSemanticTree() {
    // Creating test nodes to update.
    Node root_node = CreateTestNode(kRootNodeId, kRootNodeLabel);
    root_node.set_child_ids({kChildNodeId});

    Node child_node = CreateTestNode(kChildNodeId, kChildNodeLabel);
    std::vector<Node> update_nodes;
    update_nodes.push_back(std::move(root_node));
    update_nodes.push_back(std::move(child_node));

    // Update the nodes created above.
    semantic_provider_.UpdateSemanticNodes(std::move(update_nodes));
    RunLoopUntilIdle();

    // Commit nodes.
    semantic_provider_.CommitUpdates();
    RunLoopUntilIdle();
  }

  std::unique_ptr<MockSemanticTreeServiceFactory> factory_;
  MockSemanticTreeServiceFactory* factory_ptr_;
  sys::testing::ComponentContextProvider context_provider_;
  a11y::ViewManager view_manager_;
  a11y::GestureManager gesture_manager_;
  a11y::GestureListenerRegistry gesture_listener_registry_;
  MockGestureListener mock_gesture_listener_;

  std::unique_ptr<MockScreenReaderContext> context_;
  MockScreenReaderContext* context_ptr_;
  MockA11yFocusManager* a11y_focus_manager_ptr_;
  MockScreenReaderContext::MockSpeaker* mock_speaker_ptr_;
  a11y::ScreenReader screen_reader_;
  MockSemanticProvider semantic_provider_;
};  // namespace

TEST_F(ScreenReaderTest, OnOneFingerSingleTapAction) {
  semantic_provider_.SetHitTestResult(kRootNodeId);

  // Create OnOneFingerTap Action.
  CreateOnOneFingerTapAction();
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  // Verify that TTS is called when OneFingerTapAction was performed.
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->node_ids()[0], kRootNodeId);
}

TEST_F(ScreenReaderTest, OnOneFingerDoubleTapAction) {
  // Prepare the context of the screen reader(by setting A11yFocusInfo), assuming that it has a node
  // selected in a particular view.
  a11y_focus_manager_ptr_->SetA11yFocus(semantic_provider_.koid(), kRootNodeId,
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
  semantic_provider_.SetHitTestResult(kRootNodeId);

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
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->node_ids()[0], kRootNodeId);
}

TEST_F(ScreenReaderTest, NextAction) {
  // Update focused node.
  a11y_focus_manager_ptr_->UpdateA11yFocus(semantic_provider_.koid(), kRootNodeId);

  // Set Next Node result.
  Node next_node = CreateTestNode(kChildNodeId, kChildNodeLabel);
  factory_ptr_->semantic_tree()->SetNextNode(&next_node);

  // Create Next Action.
  glm::vec2 first_update_ndc_position = {0, -.7f};

  // Perform Up Swipe which translates to Next action.
  SendPointerEvents(DownEvents(kPointerId, {}) + MoveEvents(1, {}, first_update_ndc_position));
  SendPointerEvents(
      MoveEvents(kPointerId, first_update_ndc_position, first_update_ndc_position, 1) +
      UpEvents(kPointerId, first_update_ndc_position));
  RunLoopUntilIdle();

  ASSERT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_EQ(kChildNodeId, a11y_focus_manager_ptr_->GetA11yFocus().value().node_id);
  EXPECT_EQ(semantic_provider_.koid(),
            a11y_focus_manager_ptr_->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->node_ids()[0], kChildNodeId);
}

TEST_F(ScreenReaderTest, PreviousAction) {
  // Update focused node.
  a11y_focus_manager_ptr_->UpdateA11yFocus(semantic_provider_.koid(), kRootNodeId);

  // Set Previous Node result.
  Node previous_node = CreateTestNode(kChildNodeId, kChildNodeLabel);
  factory_ptr_->semantic_tree()->SetPreviousNode(&previous_node);

  // Create Previous Action.
  glm::vec2 first_update_ndc_position = {0, .7f};

  // Perform Down Swipe which translates to Previous action.
  SendPointerEvents(DownEvents(kPointerId, {}) + MoveEvents(1, {}, first_update_ndc_position));
  SendPointerEvents(
      MoveEvents(kPointerId, first_update_ndc_position, first_update_ndc_position, 1) +
      UpEvents(kPointerId, first_update_ndc_position));

  RunLoopUntilIdle();

  ASSERT_TRUE(a11y_focus_manager_ptr_->IsSetA11yFocusCalled());
  EXPECT_EQ(kChildNodeId, a11y_focus_manager_ptr_->GetA11yFocus().value().node_id);
  EXPECT_EQ(semantic_provider_.koid(),
            a11y_focus_manager_ptr_->GetA11yFocus().value().view_ref_koid);
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->node_ids().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->node_ids()[0], kChildNodeId);
}

TEST_F(ScreenReaderTest, ThreeFingerUpSwipeAction) {
  mock_gesture_listener_.SetOnGestureCallbackStatus(true);
  mock_gesture_listener_.SetUtterance(kListenerUtterance);
  // Set GestureType in the mock to something other than UP_SWIPE, so that when OnGesture() is
  // called, we can confirm it's called with the correct gesture type.
  mock_gesture_listener_.SetGestureType(Type::THREE_FINGER_SWIPE_DOWN);

  // Perform three finger Up Swipe action.
  SendPointerEvents(DownEvents(kPointerId, {}), 3);
  SendPointerEvents(MoveEvents(kPointerId, {}, {0, -.7f}), 3);
  SendPointerEvents(UpEvents(kPointerId, {0, -.7f}), 3);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_gesture_listener_.is_registered());
  // Up Gesture corresponds to Right Swipe.
  ASSERT_EQ(mock_gesture_listener_.gesture_type(), Type::THREE_FINGER_SWIPE_RIGHT);
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->messages().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->messages()[0], kListenerUtterance);
}

TEST_F(ScreenReaderTest, ThreeFingerDownSwipeAction) {
  mock_gesture_listener_.SetOnGestureCallbackStatus(true);
  mock_gesture_listener_.SetUtterance(kListenerUtterance);
  // Set GestureType in the mock to something other than DOWN_SWIPE, so that when OnGesture() is
  // called, we can confirm it's called with the correct gesture type.
  mock_gesture_listener_.SetGestureType(Type::THREE_FINGER_SWIPE_UP);

  // Perform three finger Down Swipe action.
  SendPointerEvents(DownEvents(kPointerId, {}), 3);
  SendPointerEvents(MoveEvents(kPointerId, {}, {0, .7f}), 3);
  SendPointerEvents(UpEvents(kPointerId, {0, .7f}), 3);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_gesture_listener_.is_registered());
  // Down Gesture corresponds to Left Swipe.
  ASSERT_EQ(mock_gesture_listener_.gesture_type(), Type::THREE_FINGER_SWIPE_LEFT);
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->messages().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->messages()[0], kListenerUtterance);
}

TEST_F(ScreenReaderTest, ThreeFingerRightSwipeAction) {
  mock_gesture_listener_.SetOnGestureCallbackStatus(true);
  mock_gesture_listener_.SetUtterance(kListenerUtterance);
  // Set GestureType in the mock to something other than RIGHT_SWIPE, so that when OnGesture() is
  // called, we can confirm it's called with the correct gesture type.
  mock_gesture_listener_.SetGestureType(Type::THREE_FINGER_SWIPE_LEFT);

  // Perform three finger Right Swipe action.
  SendPointerEvents(DownEvents(kPointerId, {}), 3);
  SendPointerEvents(MoveEvents(kPointerId, {}, {.7f, 0}), 3);
  SendPointerEvents(UpEvents(kPointerId, {.7f, 0}), 3);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_gesture_listener_.is_registered());
  // Right Gesture corresponds to Down Swipe.
  ASSERT_EQ(mock_gesture_listener_.gesture_type(), Type::THREE_FINGER_SWIPE_DOWN);
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->messages().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->messages()[0], kListenerUtterance);
}

TEST_F(ScreenReaderTest, ThreeFingerLeftSwipeAction) {
  mock_gesture_listener_.SetOnGestureCallbackStatus(true);
  mock_gesture_listener_.SetUtterance(kListenerUtterance);
  // Set GestureType in the mock to something other than LEFT_SWIPE, so that when OnGesture() is
  // called, we can confirm it's called with the correct gesture type.
  mock_gesture_listener_.SetGestureType(Type::THREE_FINGER_SWIPE_RIGHT);

  // Perform three finger Left Swipe action.
  SendPointerEvents(DownEvents(kPointerId, {}), 3);
  SendPointerEvents(MoveEvents(kPointerId, {}, {-.7f, 0}), 3);
  SendPointerEvents(UpEvents(kPointerId, {-.7f, 0}), 3);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_gesture_listener_.is_registered());
  // Left Gesture corresponds to Up Swipe.
  ASSERT_EQ(mock_gesture_listener_.gesture_type(), Type::THREE_FINGER_SWIPE_UP);
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->messages().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->messages()[0], kListenerUtterance);
}

TEST_F(ScreenReaderTest, TwoFingerSingleTapAction) {
  mock_gesture_listener_.SetOnGestureCallbackStatus(true);
  mock_gesture_listener_.SetUtterance(kListenerUtterance);

  // Perform two finger single tap action.
  SendPointerEvents(
      DownEvents(1 /* pointer ID, unused */, {}) + UpEvents(1 /* pointer ID, unused */, {}),
      2 /* number of fingers */);
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_gesture_listener_.is_registered());
  // Speech should be cancelled.
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedCancel());
}

}  // namespace
}  // namespace accessibility_test
