// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_tap_recognizer.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"
#include "src/ui/a11y/lib/testing/input.h"

namespace accessibility_test {
namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

class OneFingerTapRecognizerTest : public gtest::TestLoopFixture {
 public:
  OneFingerTapRecognizerTest()
      : recognizer_([this](a11y::GestureContext context) {
          gesture_won_ = true;
          gesture_context_ = context;
        }) {}

  void SendPointerEvents(const std::vector<PointerParams>& events) {
    for (const auto& event : events) {
      SendPointerEvent(event);
    }
  }

  void SendPointerEvent(const PointerParams& event) {
    if (member_.is_held()) {
      recognizer_.HandleEvent(ToPointerEvent(event, 0));
    }
  }

  MockContestMember member_;
  a11y::OneFingerTapRecognizer recognizer_;
  bool gesture_won_ = false;
  a11y::GestureContext gesture_context_;
};

// Keep in mind when simulating |GestureArena| behavior, only send pointer events while a contest
// member_ is held.

// Tests Gesture Detection case.
TEST_F(OneFingerTapRecognizerTest, GestureDetected) {
  recognizer_.OnContestStarted(member_.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {}});

  // After the UP event, we're expected to have released our member_.
  ASSERT_TRUE(member_.is_held());

  SendPointerEvent({1, Phase::UP, {}});

  EXPECT_FALSE(member_.is_held());
  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kAccepted);
}

// Tests Gesture Detection case where gesture is declared a winner.
TEST_F(OneFingerTapRecognizerTest, GestureDetectedWin) {
  recognizer_.OnContestStarted(member_.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {}});
  SendPointerEvent({1, Phase::UP, {}});
  recognizer_.OnWin();

  EXPECT_TRUE(gesture_won_);

  // Wait for the timeout, to make sure the scheduled task doesn't execute and crash us.
  RunLoopFor(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout);
}

// Tests Gesture Detection case where gesture is declared defeated.
TEST_F(OneFingerTapRecognizerTest, GestureDetectedLoss) {
  recognizer_.OnContestStarted(member_.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {}});
  SendPointerEvent({1, Phase::UP, {}});
  recognizer_.OnDefeat();

  EXPECT_FALSE(gesture_won_);

  // Wait for the timeout, to make sure the scheduled task doesn't execute and crash us.
  RunLoopFor(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout);
}

// Tests Gesture Detection failure, where gesture detection times out because of long press.
TEST_F(OneFingerTapRecognizerTest, GestureTimeout) {
  recognizer_.OnContestStarted(member_.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // Wait until the timeout, after which the gesture should abandon.
  RunLoopFor(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout);

  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kRejected);
  EXPECT_FALSE(gesture_won_);
}

// Tests Gesture Detection failure when multiple fingers are detected.
TEST_F(OneFingerTapRecognizerTest, MultiFingerDetected) {
  recognizer_.OnContestStarted(member_.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // New pointer ID added, but it did not make contact with the screen yet.
  SendPointerEvent({2, Phase::ADD, {}});

  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kUndecided);

  // Sends a down event with the second pointer ID, causing the gesture to be rejected.
  SendPointerEvent({2, Phase::DOWN, {}});

  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kRejected);
  EXPECT_FALSE(gesture_won_);
}

// Tests Gesture Detection failure when gesture is performed over a larger area(something like
// swipe).
TEST_F(OneFingerTapRecognizerTest, GesturePerformedOverLargerArea) {
  recognizer_.OnContestStarted(member_.TakeInterface());

  SendPointerEvents(
      DragEvents(1, {}, {a11y::OneFingerTapRecognizer::kGestureMoveThreshold + .1f, 0}));

  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kRejected);
  EXPECT_FALSE(gesture_won_);
}

// This test makes sure that local coordinates are passed correctly through the gesture context to
// the callback.
TEST_F(OneFingerTapRecognizerTest, RecognizersPassesLocalCoordinatesToCallback) {
  recognizer_.OnContestStarted(member_.TakeInterface());

  auto event = ToPointerEvent({1, Phase::ADD, {}}, 0);
  event.set_viewref_koid(100);
  event.set_local_point({2, 2});
  recognizer_.HandleEvent(event);
  event.set_phase(Phase::DOWN);
  recognizer_.HandleEvent(event);
  event.set_phase(Phase::UP);
  recognizer_.HandleEvent(event);

  recognizer_.OnWin();

  EXPECT_TRUE(gesture_won_);
  EXPECT_EQ(gesture_context_.view_ref_koid, 100u);
  EXPECT_EQ(gesture_context_.local_point->x, 2);
  EXPECT_EQ(gesture_context_.local_point->y, 2);
}

}  // namespace
}  // namespace accessibility_test
