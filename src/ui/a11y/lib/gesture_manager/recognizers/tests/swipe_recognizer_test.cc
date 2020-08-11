// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/directional_swipe_recognizers.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"

namespace accessibility_test {
namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

constexpr char kSwipeRecognizerName[] = "test_swipe_recognizer";

class TestSwipeRecognizer : public a11y::SwipeRecognizerBase {
 public:
  TestSwipeRecognizer(SwipeGestureCallback callback, uint32_t number_of_fingers)
      : SwipeRecognizerBase(std::move(callback), number_of_fingers,
                            SwipeRecognizerBase::kDefaultSwipeGestureTimeout,
                            kSwipeRecognizerName) {}

  void set_valid(bool valid) { valid_ = valid; }

  std::string DebugName() const override { return kSwipeRecognizerName; };

 private:
  bool SwipeHasValidSlopeAndDirection(float x_displacement, float y_displacement) const override {
    return valid_;
  }

  bool valid_ = true;
};

template <typename Recognizer>
class SwipeRecognizerTest : public gtest::TestLoopFixture,
                            public testing::WithParamInterface<uint32_t> {
 public:
  SwipeRecognizerTest()
      : recognizer_(
            [this](a11y::GestureContext context) {
              gesture_won_ = true;
              gesture_context_ = context;
            },
            GetParam()) {}

  bool gesture_won() const { return gesture_won_; }
  const a11y::GestureContext& gesture_context() const { return gesture_context_; }
  Recognizer* recognizer() { return &recognizer_; }

  void SendPointerEvents(const std::vector<PointerParams>& events) {
    for (const auto& event : events) {
      SendPointerEvent(event);
    }
  }

  void SendPointerEvent(const PointerParams& event) {
    recognizer_.HandleEvent(ToPointerEvent(event, 0));
  }

 private:
  Recognizer recognizer_;
  bool gesture_won_ = false;
  a11y::GestureContext gesture_context_;
};

class SwipeRecognizerBaseTest : public SwipeRecognizerTest<TestSwipeRecognizer> {};
class UpSwipeRecognizerTest : public SwipeRecognizerTest<a11y::UpSwipeGestureRecognizer> {};
class DownSwipeRecognizerTest : public SwipeRecognizerTest<a11y::DownSwipeGestureRecognizer> {};
class LeftSwipeRecognizerTest : public SwipeRecognizerTest<a11y::LeftSwipeGestureRecognizer> {};
class RightSwipeRecognizerTest : public SwipeRecognizerTest<a11y::RightSwipeGestureRecognizer> {};

INSTANTIATE_TEST_SUITE_P(SwipeRecognizerBaseTestWithParams, SwipeRecognizerBaseTest,
                         ::testing::Values(1, 2, 3));
INSTANTIATE_TEST_SUITE_P(UpSwipeRecognizerTestWithParams, UpSwipeRecognizerTest,
                         ::testing::Values(1, 2, 3));
INSTANTIATE_TEST_SUITE_P(DownSwipeRecognizerTestWithParams, DownSwipeRecognizerTest,
                         ::testing::Values(1, 2, 3));
INSTANTIATE_TEST_SUITE_P(LeftSwipeRecognizerTestWithParams, LeftSwipeRecognizerTest,
                         ::testing::Values(1, 2, 3));
INSTANTIATE_TEST_SUITE_P(RightSwipeRecognizerTestWithParams, RightSwipeRecognizerTest,
                         ::testing::Values(1, 2, 3));

TEST_P(SwipeRecognizerBaseTest, Win) {
  recognizer()->OnWin();
  EXPECT_TRUE(gesture_won());
}

TEST_P(SwipeRecognizerBaseTest, Defeat) {
  recognizer()->OnDefeat();
  EXPECT_FALSE(gesture_won());
}

// Tests Gesture Detection failure when less fingers are detected than expected.
// Also covers the case, when Up event is detected before all the Down events are detected.
// This test case applies only to cases where the number of fingers is more than 1.
TEST_P(SwipeRecognizerBaseTest, RejectLessThanExpectedFinger) {
  if (GetParam() == 1) {
    return;
  }
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam() - 1; finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 0; finger < GetParam() - 1; finger++) {
    SendPointerEvent({finger, Phase::UP, {0, .7f}});
  }

  EXPECT_FALSE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests Gesture Detection failure when more fingers are detected than expected.
TEST_P(SwipeRecognizerBaseTest, RejectMoreThanExpectedFinger) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }

  // New pointer ID added, but it did not make contact with the screen yet.
  SendPointerEvent({GetParam() + 1, Phase::ADD, {}});
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);

  // Sends a down event with the new pointer ID, causing the gesture to be rejected.
  SendPointerEvent({GetParam() + 1, Phase::DOWN, {}});
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Test Gesture detection failure when a Down event for a finger is detected after Up event was
// detected for any other finger.
// This doesn't apply when the number of fingers is 1.
TEST_P(SwipeRecognizerBaseTest, RejectDownEventAfterFirstUp) {
  if (GetParam() == 1) {
    return;
  }
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  // Send Down events for all but 1 finger.
  for (uint32_t finger = 0; finger < GetParam() - 1; finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }

  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  // Send Up event for the first finger.
  SendPointerEvent({0, Phase::UP, {0, .7f}});

  // Send the last Down event.
  SendPointerEvents(DownEvents(GetParam(), {}));

  EXPECT_FALSE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Test Gesture detection failure when a Move event for a finger is detected before Down event.
// This doesn't apply when the number of fingers is 1.
TEST_P(SwipeRecognizerBaseTest, RejectMoveEventBeforeDown) {
  if (GetParam() == 1) {
    return;
  }
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  // Send the first Down event.
  SendPointerEvents(DownEvents(0, {}));

  // Send Move event for the next finger.
  SendPointerEvent({1, Phase::MOVE, {}});

  EXPECT_FALSE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

TEST_P(SwipeRecognizerBaseTest, Timeout) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }

  RunLoopFor(a11y::SwipeRecognizerBase::kDefaultSwipeGestureTimeout);
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

TEST_P(SwipeRecognizerBaseTest, NoTimeoutAfterDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {0, .7f}});
  }

  // By now, the member has been released (verified in the |Accept| test), so state can no longer
  // change. Wait for the timeout, to make sure the scheduled task doesn't execute and crash us.
  RunLoopFor(a11y::SwipeRecognizerBase::kDefaultSwipeGestureTimeout);
}

// Tests rejection case in which the swipe gesture does not cover long enough distance.
TEST_P(SwipeRecognizerBaseTest, RejectWhenDistanceTooSmall) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {0, .2f}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Ensures that the test recognizer, which considers all swipe paths valid by default, calls
// |Accept| on |UP|. The base recognizer still validates swipe distance.
TEST_P(SwipeRecognizerBaseTest, Accept) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }

  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {0, .7f}});
  }

  EXPECT_FALSE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

// Tests case in which swipe gesture covers a large distance. We are using the entire upper range,
// so there is no case where the distance between Up and Down is more than 1NDC.
TEST_P(SwipeRecognizerBaseTest, AcceptWhenDistanceIsLarge) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  // UP event must be between .25 and 1 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {0, 1}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

TEST_P(UpSwipeRecognizerTest, MoveEventAtSameLocationAsDown) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
    SendPointerEvent({finger, Phase::MOVE, {}});
  }
  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);
}

TEST_P(UpSwipeRecognizerTest, GestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(MoveEvents(finger, {}, {0, -.7f}));
  }
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {0, -.7f}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

// Test Gesture detection case when a long move event is detected for a finger after first UP
// event is detected.
// This test is applicable only when number of finger is more than 1.
TEST_P(UpSwipeRecognizerTest, RejectLongMoveEventAfterFirstUp) {
  if (GetParam() == 1) {
    return;
  }

  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  // Send all the down events.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }

  // Send all the move events.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(MoveEvents(finger, {}, {0, -.7f}));
  }

  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);

  // Send first Up event.
  SendPointerEvent({0, Phase::UP, {0, -.7f}});

  // Move finger over a larger distance.
  SendPointerEvent({1, Phase::MOVE, {0, -.9f}});

  // Send remaining Up events.
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 1; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {0, -.9f}});
  }

  EXPECT_FALSE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

TEST_P(DownSwipeRecognizerTest, MoveEventAtSameLocationAsDown) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
    SendPointerEvent({finger, Phase::MOVE, {}});
  }
  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);
}

TEST_P(DownSwipeRecognizerTest, GestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(MoveEvents(finger, {}, {0, .7f}));
  }
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {0, .7f}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

// Test Gesture detection case when a long move event is detected for a finger after first UP
// event is detected.
// This test is applicable only when number of finger is more than 1.
TEST_P(DownSwipeRecognizerTest, RejectLongMoveEventAfterFirstUp) {
  if (GetParam() == 1) {
    return;
  }

  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  // Send all the down events.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }

  // Send all the move events.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(MoveEvents(finger, {}, {0, .7f}));
  }

  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);

  // Send first Up event.
  SendPointerEvent({0, Phase::UP, {0, .7f}});

  // Move finger over a larger distance.
  SendPointerEvent({1, Phase::MOVE, {0, .9f}});

  // Send remaining Up events.
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 1; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {0, .9f}});
  }

  EXPECT_FALSE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

TEST_P(RightSwipeRecognizerTest, MoveEventAtSameLocationAsDown) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
    SendPointerEvent({finger, Phase::MOVE, {}});
  }
  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);
}

TEST_P(RightSwipeRecognizerTest, GestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(MoveEvents(finger, {}, {.7f, 0}));
  }
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {.7f, 0}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

// Test Gesture detection case when a long move event is detected for a finger after first UP
// event is detected.
// This test is applicable only when number of finger is more than 1.
TEST_P(RightSwipeRecognizerTest, RejectLongMoveEventAfterFirstUp) {
  if (GetParam() == 1) {
    return;
  }

  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  // Send all the down events.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }

  // Send all the move events.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(MoveEvents(finger, {}, {.7f, 0}));
  }

  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);

  // Send first Up event.
  SendPointerEvent({0, Phase::UP, {.7f, 0}});

  // Move finger over a larger distance.
  SendPointerEvent({1, Phase::MOVE, {.9f, 0}});

  // Send remaining Up events.
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 1; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {.9f, 0}});
  }

  EXPECT_FALSE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

TEST_P(LeftSwipeRecognizerTest, MoveEventAtSameLocationAsDown) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
    SendPointerEvent({finger, Phase::MOVE, {}});
  }
  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);
}

TEST_P(LeftSwipeRecognizerTest, GestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(MoveEvents(finger, {}, {-.7f, 0}));
  }
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {-.7f, 0}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

// Test Gesture detection case when a long move event is detected for a finger after first UP
// event is detected.
// This test is applicable only when number of finger is more than 1.
TEST_P(LeftSwipeRecognizerTest, RejectLongMoveEventAfterFirstUp) {
  if (GetParam() == 1) {
    return;
  }

  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  // Send all the down events.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }

  // Send all the move events.
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(MoveEvents(finger, {}, {-.7f, 0}));
  }

  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);

  // Send first Up event.
  SendPointerEvent({0, Phase::UP, {-.7f, 0}});

  // Move finger over a larger distance.
  SendPointerEvent({1, Phase::MOVE, {-.9f, 0}});

  // Send remaining Up events.
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 1; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {-.9f, 0}});
  }

  EXPECT_FALSE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

// Tests rejection case for upward swipe in which up gesture ends too far from vertical.
TEST_P(UpSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {.5f, -.5f}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for upward swipe in which gesture takes invalid path. Every swipe has cone
// like area in which the gesture is valid. This test is checking that if swipe falls outside of
// this cone then its rejected.
TEST_P(UpSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::MOVE, {0, .3f}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for downward swipe in which gesture ends in an invalid location.
TEST_P(DownSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {-.5f, .5f}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for downward swipe in which gesture takes invalid path. Every swipe has cone
// like area in which the gesture is valid. This test is checking that if swipe falls outside of
// this cone then its rejected.
TEST_P(DownSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::MOVE, {0, -.3f}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for right swipe in which gesture ends in an invalid location.
TEST_P(RightSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {.5f, .5f}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for right swipe in which gesture takes invalid path. Every swipe has cone
// like area in which the gesture is valid. This test is checking that if swipe falls outside of
// this cone then its rejected.
TEST_P(RightSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::MOVE, {-.3f, 0}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for left swipe in which gesture ends in an invalid location.
TEST_P(LeftSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::UP, {-.5f, -.5f}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for left swipe in which gesture takes invalid path. Every swipe has cone
// like area in which the gesture is valid. This test is checking that if swipe falls outside of
// this cone then its rejected.
TEST_P(LeftSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvents(DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < GetParam(); finger++) {
    SendPointerEvent({finger, Phase::MOVE, {.3f, 0}});
  }

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

}  // namespace
}  // namespace accessibility_test
