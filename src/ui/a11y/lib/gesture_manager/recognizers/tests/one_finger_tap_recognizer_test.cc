// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_tap_recognizer.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <memory>

#include "gtest/gtest.h"
#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"
#include "src/ui/a11y/lib/testing/input.h"

namespace accessibility_test {

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
    recognizer_.HandleEvent(ToPointerEvent(event, 0));
  }

  a11y::OneFingerTapRecognizer recognizer_;
  bool gesture_won_ = false;
  a11y::GestureContext gesture_context_;
};

// Constraints to keep in mind in simulating |GestureArena| behavior:
// * Only send pointer events while a contest member is held.
// * If a member is held, set its status before calling |OnWin|/|OnDefeat|.

// Tests Gesture Detection case, where gesture is detected first by the recognizer and then it is
// declared a winner by GestureArena.
TEST_F(OneFingerTapRecognizerTest, WonAfterGestureDetected) {
  MockContestMember member;
  recognizer_.OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {}});

  // After the UP event, we're expected to have released our member to go passive.
  ASSERT_TRUE(member);

  SendPointerEvent({1, Phase::UP, {}});

  EXPECT_FALSE(member);
  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won_);

  recognizer_.OnWin();

  EXPECT_TRUE(gesture_won_);

  // Wait for the timeout, to make sure the scheduled task doesn't execute and crash us.
  RunLoopFor(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout);
}

// Tests Gesture Detection case, where gesture is detected by the recognizer after it is
// declared a winner by GestureArena.
TEST_F(OneFingerTapRecognizerTest, GestureDetectedAfterWinning) {
  MockContestMember member;
  recognizer_.OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // OnWin() before gesture is completed.
  member.SetStatus(a11y::ContestMember::Status::kWinner);
  recognizer_.OnWin();

  EXPECT_FALSE(gesture_won_);
  ASSERT_TRUE(member);

  // Sends an UP event, this should detect gesture.
  SendPointerEvent({1, Phase::UP, {}});

  EXPECT_TRUE(gesture_won_);

  EXPECT_FALSE(member);
  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_FALSE(member.IsRejectCalled());
}

// Tests Gesture Detection failure, where gesture detection times out because of long press.
TEST_F(OneFingerTapRecognizerTest, GestureTimeout) {
  MockContestMember member;
  recognizer_.OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // Wait until the timeout, after which the gesture should abandon.
  RunLoopFor(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout);

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won_);
}

// Tests Gesture Detection failure, where recognizer is declared a winner by GestureArena but
// gesture detection timed out.
TEST_F(OneFingerTapRecognizerTest, WonButGestureTimedOut) {
  MockContestMember member;
  recognizer_.OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // OnWin() before gesture is completed.
  member.SetStatus(a11y::ContestMember::Status::kWinner);
  recognizer_.OnWin();

  RunLoopFor(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout);

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled()) << "Should have rejected due to timeout.";
  EXPECT_FALSE(gesture_won_);
}

// Tests Gesture Detection failure when multiple fingers are detected.
TEST_F(OneFingerTapRecognizerTest, MultiFingerDetected) {
  MockContestMember member;
  recognizer_.OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // New pointer ID added, but it did not make contact with the screen yet.
  SendPointerEvent({2, Phase::ADD, {}});

  EXPECT_FALSE(member.IsRejectCalled());

  // Sends a down event with the second pointer ID, causing the gesture to be rejected.
  SendPointerEvent({2, Phase::DOWN, {}});

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won_);
}

// Tests Gesture Detection failure when gesture is performed over a larger area(something like
// swipe).
TEST_F(OneFingerTapRecognizerTest, GesturePerformedOverLargerArea) {
  MockContestMember member;
  recognizer_.OnContestStarted(member.TakeInterface());

  SendPointerEvents(
      DragEvents(1, {}, {a11y::OneFingerTapRecognizer::kGestureMoveThreshold + .1f, 0}));

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won_);
}

// This test makes sure that local coordinates are passed correctly through the gesture context to
// the callback.
TEST_F(OneFingerTapRecognizerTest, RecognizersPassesLocalCoordinatesToCallback) {
  MockContestMember member;
  recognizer_.OnContestStarted(member.TakeInterface());

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

}  // namespace accessibility_test
