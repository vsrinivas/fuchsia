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
#include "src/lib/fxl/logging.h"
#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_arena_member.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace accessibility_test {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

class OneFingerTapRecognizerTest : public gtest::TestLoopFixture {
 public:
  OneFingerTapRecognizerTest()
      : one_finger_tap_recognizer_(
            [this](a11y::GestureContext context) {
              gesture_won_ = true;
              gesture_context_ = context;
            },
            a11y::OneFingerTapRecognizer::kOneFingerTapTimeout){};
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
  event.set_global_point({4, 4});
  event.set_viewref_koid(100);
  event.set_local_point({2, 2});
  return event;
}

// Tests Gesture Detection case, where gesture is detected first by the recognizer and then it is
// declared a winner by GestureArena.
TEST_F(OneFingerTapRecognizerTest, WonAfterGestureDetected) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsDeclareDefeatCalled());
  EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
  EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
            a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);

  {
    // Sends an Add event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kInProgress);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::MOVE);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kGestureDetectedAndWaiting);
  }

  {
    // OnWin() should call StopRoutingPointerEvents().
    member.CallOnWin();
    // Wait for the timeout, to make sure Scheduled task has not executed.
    RunLoopUntil(zx::time(0) + zx::msec(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout));

    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_TRUE(member.IsOnWinCalled());
    EXPECT_TRUE(gesture_won_);
    EXPECT_TRUE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }
}

// Tests Gesture Detection case, where gesture is detected by the recognizer after it is
// declared a winner by GestureArena.
TEST_F(OneFingerTapRecognizerTest, GestureDetectedAfterWinning) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsDeclareDefeatCalled());
  EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
  EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
            a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);

  {
    // Sends an Add event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kInProgress);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::MOVE);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // OnWin() before gesture is performed.
    member.CallOnWin();

    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_TRUE(member.IsOnWinCalled());
    EXPECT_FALSE(gesture_won_);
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, this should detect gesture and StopRoutingPointerEvents should get called.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_TRUE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }
}

// Tests Gesture Detection case, where gesture detection timesout because  of no events.
TEST_F(OneFingerTapRecognizerTest, GestureTimeout) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsDeclareDefeatCalled());
  EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
  EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
            a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);

  {
    // Sends an Add event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kInProgress);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  // Wait for twice as long as the timeout, to simulate gesture taking more time than timeout.
  // This should result in DeclareDefeat() task to get executed. Which should in turn call
  // OnDefeat() and Reset the entire state.
  RunLoopUntil(zx::time(0) + zx::msec(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout * 2));
  EXPECT_TRUE(member.IsDeclareDefeatCalled());
  EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
  EXPECT_FALSE(gesture_won_);
  EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
            a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
}

// Tests Gesture Detection failure when gesture is performed over a longer period of time.
TEST_F(OneFingerTapRecognizerTest, GestureTakingLongerThanTimeout) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsDeclareDefeatCalled());
  EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
  EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
            a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);

  {
    // Sends an Add event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kInProgress);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::MOVE);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    // Event time is in Nano seconds while Timeout is in milliseconds.
    event.set_event_time(
        (event.event_time() + a11y::OneFingerTapRecognizer::kOneFingerTapTimeout + 1) * 1000);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_TRUE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }
}

// Tests Gesture Detection case, where recognizer is declared a winner by GestureArena but gesture
// detection timedout.
TEST_F(OneFingerTapRecognizerTest, WonButGestureTimedout) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsDeclareDefeatCalled());
  EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
  EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
            a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);

  {
    // Sends an Add event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kInProgress);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::MOVE);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // OnWin() before gesture is performed.
    member.CallOnWin();
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_TRUE(member.IsOnWinCalled());
    EXPECT_FALSE(gesture_won_);
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, this should detect gesture and StopRoutingPointerEvents should get called.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    // Wait for more than timeout before sending the Up gesture to complete the gesture.
    // Since recognizer is already the winner, DeclareDefeat() Should not be called.
    RunLoopUntil(zx::time(0) + zx::msec(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout * 2));
    // Update event time(in nano second.);
    event.set_event_time(event.event_time() +
                         (a11y::OneFingerTapRecognizer::kOneFingerTapTimeout * 2 * 1000));
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_TRUE(member.IsOnWinCalled());
    EXPECT_TRUE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }
}

// Tests Gesture Detection failure when multiple fingers are detected.
TEST_F(OneFingerTapRecognizerTest, MultiFingerDetected) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsDeclareDefeatCalled());
  EXPECT_FALSE(member.IsOnWinCalled());
  EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
  EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
            a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);

  {
    // Sends an Add event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kInProgress);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::MOVE);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    event.set_pointer_id(2);
    one_finger_tap_recognizer_.HandleEvent(event);

    // Wait for more than the timeout.
    RunLoopUntil(zx::time(0) + zx::msec(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout * 2));
    EXPECT_TRUE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }
}

// Tests Gesture Detection failure when gesture is performed over a larger area(something like
// swipe).
TEST_F(OneFingerTapRecognizerTest, GesturePerformedOverLargerArea) {
  MockArenaMember member(&one_finger_tap_recognizer_);
  one_finger_tap_recognizer_.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsDeclareDefeatCalled());
  EXPECT_FALSE(member.IsOnWinCalled());
  EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
  EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
            a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);

  {
    // Sends an Add event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kInProgress);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::MOVE);
    one_finger_tap_recognizer_.HandleEvent(event);
    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    ::fuchsia::math::PointF position;
    position.x = event.global_point().x + a11y::OneFingerTapRecognizer::kGestureMoveThreshold + 1;
    position.x = event.global_point().y + a11y::OneFingerTapRecognizer::kGestureMoveThreshold + 1;
    event.set_global_point(position);

    one_finger_tap_recognizer_.HandleEvent(event);
    RunLoopUntilIdle();
    EXPECT_TRUE(member.IsDeclareDefeatCalled());
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_FALSE(member.IsStopRoutingPointerEventsCalled());
    EXPECT_EQ(one_finger_tap_recognizer_.GetGestureState(),
              a11y::OneFingerTapRecognizer::TapGestureState::kNotStarted);
  }
}

// This test makes sure that local coordinates are passed correctly through the gesture context  to
// the callback.
TEST_F(OneFingerTapRecognizerTest, RecognizersPassesLocalCoordinatesToCallback) {
  MockArenaMember member(&one_finger_tap_recognizer_);
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

  {
    member.CallOnWin();
    // Wait for the timeout, to make sure Scheduled task has not executed.
    RunLoopUntil(zx::time(0) + zx::msec(a11y::OneFingerTapRecognizer::kOneFingerTapTimeout));

    EXPECT_FALSE(member.IsDeclareDefeatCalled());
    EXPECT_TRUE(member.IsOnWinCalled());
    EXPECT_TRUE(gesture_won_);
    EXPECT_EQ(gesture_context_.view_ref_koid, 100u);
    EXPECT_EQ(gesture_context_.local_point->x, 2);
    EXPECT_EQ(gesture_context_.local_point->y, 2);
  }
}

}  // namespace accessibility_test
