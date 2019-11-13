// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_tap_recognizer.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <memory>

#include "gtest/gtest.h"
#include "lib/fit/single_threaded_executor.h"
#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_arena_member.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace accessibility_test {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

class OneFingerTapRecognizerTest : public gtest::TestLoopFixture {
 public:
  OneFingerTapRecognizerTest()
      : one_finger_tap_recognizer_([this](a11y::GestureContext context) {
          gesture_won_ = true;
          gesture_context_ = context;
        }){};
  a11y::OneFingerTapRecognizer one_finger_tap_recognizer_;
  bool gesture_won_ = false;
  a11y::GestureContext gesture_context_;
};

// Returns a default Accessibility Pointer Event.
AccessibilityPointerEvent GetDefaultPointerEvent() {
  AccessibilityPointerEvent event;
  event.set_event_time(10);
  event.set_device_id(1);
  event.set_pointer_id(1);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(Phase::ADD);
  event.set_ndc_point({.5, .5});
  event.set_viewref_koid(100);
  event.set_local_point({2, 2});
  return event;
}

// Tests Gesture Detection case, where gesture is detected first by the recognizer and then it is
// declared a winner by GestureArena.
TEST_F(OneFingerTapRecognizerTest, WonAfterGestureDetected) {
  gesture_won_ = false;
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
            a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);

  {
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::MOVE);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kGestureDetected);
  }

  {
    member.CallOnWin();
    // Wait for the timeout, to make sure Scheduled task has not executed.
    RunLoopFor(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout);

    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_TRUE(gesture_won_);
  }
}

// Tests Gesture Detection case, where gesture is detected by the recognizer after it is
// declared a winner by GestureArena.
TEST_F(OneFingerTapRecognizerTest, GestureDetectedAfterWinning) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  {
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // OnWin() before gesture is performed.
    member.CallOnWin();

    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_FALSE(gesture_won_);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, this should detect gesture.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_TRUE(gesture_won_);
  }
}

// Tests Gesture Detection case, where gesture detection timesout because  of no events.
TEST_F(OneFingerTapRecognizerTest, GestureTimeout) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  {
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  // Wait for twice as long as the timeout, to simulate gesture taking more time than timeout.
  // This should result in Reject() task to get executed. Which should in turn call
  // OnDefeat().
  RunLoopFor(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout * 2);
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won_);
  EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
            a11y::OneFingerTapRecognizer::TapGestureState::kDone);
}

// Tests Gesture Detection case, where recognizer is declared a winner by GestureArena but gesture
// detection timed out.
TEST_F(OneFingerTapRecognizerTest, WonButGestureTimedOut) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  {
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // OnWin() before gesture is performed.
    member.CallOnWin();
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_FALSE(gesture_won_);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, this should detect gesture but not execute it, as it timed out.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    // Wait for more than timeout before sending the Up gesture to complete the gesture.
    RunLoopFor(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout * 2);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_TRUE(member.IsRejectCalled());  // because it timed out.
    EXPECT_TRUE(member.IsOnWinCalled());   // because it was the winner before rejecting.
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDone);
    EXPECT_FALSE(gesture_won_);  // should not execute gesture callback.
  }
}

// Tests Gesture Detection failure when multiple fingers are detected.
TEST_F(OneFingerTapRecognizerTest, MultiFingerDetected) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  {
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // New pointer ID added, but it did not make contact with the screen yet.
    auto event = GetDefaultPointerEvent();
    event.set_pointer_id(2);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends a down event with the second pointer ID, causing the gesture to be rejected.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    event.set_pointer_id(2);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_TRUE(member.IsRejectCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDone);
  }
}

// Tests Gesture Detection failure when gesture is performed over a larger area(something like
// swipe).
TEST_F(OneFingerTapRecognizerTest, GesturePerformedOverLargerArea) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  {
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::MOVE);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    ::fuchsia::math::PointF position;
    position.x = event.ndc_point().x + a11y::OneFingerTapRecognizer::kGestureMoveThreshold + 1;
    position.x = event.ndc_point().y + a11y::OneFingerTapRecognizer::kGestureMoveThreshold + 1;
    event.set_ndc_point(position);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_TRUE(member.IsRejectCalled());
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDone);
  }
}

// This test makes sure that local coordinates are passed correctly through the gesture context  to
// the callback.
TEST_F(OneFingerTapRecognizerTest, RecognizersPassesLocalCoordinatesToCallback) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  gesture_won_ = false;
  one_finger_tap_recognizer_.AddArenaMember(&member);

  one_finger_tap_recognizer_.HandleEvent(GetDefaultPointerEvent());

  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
  }

  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::MOVE);
    one_finger_tap_recognizer_.HandleEvent(event);
  }

  {
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    one_finger_tap_recognizer_.HandleEvent(event);
  }

  member.CallOnWin();
  EXPECT_TRUE(gesture_won_);
  EXPECT_EQ(gesture_context_.view_ref_koid, 100u);
  EXPECT_EQ(gesture_context_.local_point->x, 2);
  EXPECT_EQ(gesture_context_.local_point->y, 2);
}

}  // namespace accessibility_test
