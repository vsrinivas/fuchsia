// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <memory>

#include "gtest/gtest.h"
#include "lib/fit/single_threaded_executor.h"
#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_arena_member.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/directional_swipe_recognizers.h"

namespace accessibility_test {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;
using SwipeRecognizerTest = gtest::TestLoopFixture;

// Returns a default Accessibility Pointer Event.
AccessibilityPointerEvent GetDefaultSwipePointerEvent() {
  AccessibilityPointerEvent event;
  event.set_event_time(0);
  event.set_device_id(1);
  event.set_pointer_id(1);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(Phase::ADD);
  event.set_ndc_point({0, 0});
  event.set_viewref_koid(100);
  event.set_local_point({0, 0});
  return event;
}

// Tests up swipe detection case.
TEST_F(SwipeRecognizerTest, WonAfterUpGestureDetected) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::UpSwipeGestureRecognizer up_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&up_swipe_recognizer);
  up_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({0, .1f});
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::UP);

    // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
    // a swipe.
    event.set_ndc_point({0, .7f});
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDone);
  }

  {
    // Wait for the timeout, to make sure Scheduled task has not executed.
    RunLoopFor(a11y::SwipeRecognizerBase::kSwipeGestureTimeout);

    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_TRUE(gesture_won);
  }
}

// Tests down swipe detection case.
TEST_F(SwipeRecognizerTest, WonAfterDownGestureDetected) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::DownSwipeGestureRecognizer down_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&down_swipe_recognizer);
  down_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    down_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    down_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({0, -.1f});
    down_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::UP);

    // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
    // a swipe.
    event.set_ndc_point({0, -.7f});
    down_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDone);
  }

  {
    // Wait for the timeout, to make sure Scheduled task has not executed.
    RunLoopFor(a11y::SwipeRecognizerBase::kSwipeGestureTimeout);

    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_TRUE(gesture_won);
  }
}

// Tests right swipe detection case.
TEST_F(SwipeRecognizerTest, WonAfterRightGestureDetected) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::RightSwipeGestureRecognizer right_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&right_swipe_recognizer);
  right_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({.1f, 0});
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::UP);

    // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
    // a swipe.
    event.set_ndc_point({.7f, 0});
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDone);
  }

  {
    // Wait for the timeout, to make sure Scheduled task has not executed.
    RunLoopFor(a11y::SwipeRecognizerBase::kSwipeGestureTimeout);

    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_TRUE(gesture_won);
  }
}

// Tests left swipe detection case.
TEST_F(SwipeRecognizerTest, WonAfterLeftGestureDetected) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::LeftSwipeGestureRecognizer left_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&left_swipe_recognizer);
  left_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({-.1f, 0});
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::UP);

    // up event must be between .375 and .75 NDC from DOWN event for gesture to be considered
    // a swipe.
    event.set_ndc_point({-.7f, 0});
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDone);
  }

  {
    // Wait for the timeout, to make sure Scheduled task has not executed.
    RunLoopFor(a11y::SwipeRecognizerBase::kSwipeGestureTimeout);

    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_TRUE(gesture_won);
  }
}

// Tests rejection case in which swipe gesture does not cover long enough distance.
TEST_F(SwipeRecognizerTest, RejectWhenDistanceTooSmall) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::UpSwipeGestureRecognizer up_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&up_swipe_recognizer);
  up_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({0, .1f});
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::UP);

    // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
    // a swipe.
    event.set_ndc_point({0, .2f});
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
    EXPECT_TRUE(member.IsRejectCalled());
    EXPECT_FALSE(gesture_won);
  }
}

// Tests rejection case in which swipe gesture covers too large a distance.
TEST_F(SwipeRecognizerTest, RejectWhenDistanceTooLarge) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::UpSwipeGestureRecognizer up_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&up_swipe_recognizer);
  up_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({0, .1f});
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::UP);

    // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
    // a swipe.
    event.set_ndc_point({0, 2.f});
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
    EXPECT_TRUE(member.IsRejectCalled());
  }
}

// Tests rejection case in which swipe gesture exceeds timeout.
TEST_F(SwipeRecognizerTest, RejectWhenTimeoutExceeded) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::UpSwipeGestureRecognizer up_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&up_swipe_recognizer);
  up_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({0, .1f});
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  RunLoopFor(a11y::SwipeRecognizerBase::kSwipeGestureTimeout);

  EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  EXPECT_TRUE(member.IsRejectCalled());
}

// Tests rejection case for upward swipe in which up gesture ends too far from vertical.
TEST_F(SwipeRecognizerTest, RejectUpSwipeOnInvalidEndLocation) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::UpSwipeGestureRecognizer up_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&up_swipe_recognizer);
  up_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({0, .1f});
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::UP);

    // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
    // a swipe.
    event.set_ndc_point({.5f, .5f});
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
    EXPECT_TRUE(member.IsRejectCalled());
  }
}

// Tests rejection case for upward swipe in which gesture takes invalid path.
TEST_F(SwipeRecognizerTest, RejectUpSwipeOnInvalidPath) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::UpSwipeGestureRecognizer up_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&up_swipe_recognizer);
  up_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({0, -.1f});
    up_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(up_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
    EXPECT_TRUE(member.IsRejectCalled());
  }
}

// Tests rejection case for downward swipe in which gesture ends in an invalid location.
TEST_F(SwipeRecognizerTest, RejectDownSwipeOnInvalidEndLocation) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::DownSwipeGestureRecognizer down_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&down_swipe_recognizer);
  down_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    down_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    down_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({0, -.1f});
    down_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::UP);

    event.set_ndc_point({-.5f, -.5f});
    down_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
    EXPECT_TRUE(member.IsRejectCalled());
  }
}

// Tests rejection case for downward swipe in which gesture takes invalid path.
TEST_F(SwipeRecognizerTest, RejectDownSwipeOnInvalidPath) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::DownSwipeGestureRecognizer down_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&down_swipe_recognizer);
  down_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    down_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    down_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({0, .1f});
    down_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(down_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
    EXPECT_TRUE(member.IsRejectCalled());
  }
}

// Tests rejection case for right swipe in which gesture ends in an invalid location.
TEST_F(SwipeRecognizerTest, RejectRightSwipeOnInvalidEndLocation) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::RightSwipeGestureRecognizer right_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&right_swipe_recognizer);
  right_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({.1f, 0});
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::UP);

    event.set_ndc_point({.5f, -.5f});
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
    EXPECT_TRUE(member.IsRejectCalled());
  }
}

// Tests rejection case for right swipe in which gesture takes invalid path.
TEST_F(SwipeRecognizerTest, RejectRightSwipeOnInvalidPath) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::RightSwipeGestureRecognizer right_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&right_swipe_recognizer);
  right_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({-.1f, 0});
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
    EXPECT_TRUE(member.IsRejectCalled());
  }
}

// Tests rejection case for left swipe in which gesture ends in an invalid location.
TEST_F(SwipeRecognizerTest, RejectLeftSwipeOnInvalidEndLocation) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::LeftSwipeGestureRecognizer left_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&left_swipe_recognizer);
  left_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({-.1f, 0});
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::UP);

    event.set_ndc_point({-.5f, -.5f});
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
    EXPECT_TRUE(member.IsRejectCalled());
  }
}

// Tests rejection case for left swipe in which gesture takes invalid path.
TEST_F(SwipeRecognizerTest, RejectLeftSwipeOnInvalidPath) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::LeftSwipeGestureRecognizer left_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&left_swipe_recognizer);
  left_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({.1f, 0});
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
    EXPECT_TRUE(member.IsRejectCalled());
  }
}

// Tests Gesture Detection failure when multiple fingers are detected.
TEST_F(SwipeRecognizerTest, MultiFingerDetected) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::LeftSwipeGestureRecognizer left_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&left_swipe_recognizer);
  left_swipe_recognizer.AddArenaMember(&member);

  {
    auto event = GetDefaultSwipePointerEvent();
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsOnWinCalled());
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // New pointer ID added, but it did not make contact with the screen yet.
    auto event = GetDefaultSwipePointerEvent();
    event.set_pointer_id(2);
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a down event with the second pointer ID, causing the gesture to be rejected.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    event.set_pointer_id(2);
    left_swipe_recognizer.HandleEvent(event);
    EXPECT_TRUE(member.IsRejectCalled());
    EXPECT_EQ(left_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }
}

// Tests right swipe detection after member is declared winner.
TEST_F(SwipeRecognizerTest, RecognizeAfterWin) {
  a11y::GestureContext gesture_context;
  bool gesture_won = false;

  a11y::RightSwipeGestureRecognizer right_swipe_recognizer(
      [&gesture_won, &gesture_context](a11y::GestureContext context) {
        gesture_won = true;
        gesture_context = context;
      });

  MockArenaMember member(&right_swipe_recognizer);
  right_swipe_recognizer.AddArenaMember(&member);

  // Check initial state of arena member.
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
            a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);

  {
    auto event = GetDefaultSwipePointerEvent();
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kNotStarted);
  }

  {
    // Sends a Down event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::DOWN);
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Calling OnWin() before gesture is recognized should not affect state.
    member.CallOnWin();
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_FALSE(gesture_won);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends a Move event, and expects the state of Gesture to stay the same.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::MOVE);
    event.set_ndc_point({.1f, 0});
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_FALSE(member.IsRejectCalled());
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDownFingerDetected);
  }

  {
    // Sends an UP event, and expects the state of Gesture to change.
    auto event = GetDefaultSwipePointerEvent();
    event.set_phase(Phase::UP);

    // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
    // a swipe.
    event.set_ndc_point({.5f, 0});
    right_swipe_recognizer.HandleEvent(event);
    EXPECT_EQ(right_swipe_recognizer.GetGestureState(),
              a11y::SwipeRecognizerBase::SwipeGestureState::kDone);
  }
}

}  // namespace accessibility_test
