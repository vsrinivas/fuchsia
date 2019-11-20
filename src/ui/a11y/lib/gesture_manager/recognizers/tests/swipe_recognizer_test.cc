// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <memory>

#include "gtest/gtest.h"
#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/directional_swipe_recognizers.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"

namespace accessibility_test {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

template <typename Recognizer>
class SwipeRecognizerTest : public gtest::TestLoopFixture {
 public:
  SwipeRecognizerTest()
      : recognizer_([this](a11y::GestureContext context) {
          gesture_won_ = true;
          gesture_context_ = context;
        }) {}

  bool gesture_won() const { return gesture_won_; }
  a11y::GestureContext& gesture_context() const { return gesture_context_; }
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

class UpSwipeRecognizerTest : public SwipeRecognizerTest<a11y::UpSwipeGestureRecognizer> {};
class DownSwipeRecognizerTest : public SwipeRecognizerTest<a11y::DownSwipeGestureRecognizer> {};
class LeftSwipeRecognizerTest : public SwipeRecognizerTest<a11y::LeftSwipeGestureRecognizer> {};
class RightSwipeRecognizerTest : public SwipeRecognizerTest<a11y::RightSwipeGestureRecognizer> {};

// Tests up swipe detection case.
TEST_F(UpSwipeRecognizerTest, WonAfterGestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {0, -.7f}));

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_FALSE(gesture_won());

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {0, -.7f}});

  EXPECT_FALSE(member);
  EXPECT_TRUE(member.IsAcceptCalled());
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_TRUE(gesture_won());

  // Wait for the timeout, to make sure the scheduled task doesn't execute and crash us.
  RunLoopFor(a11y::SwipeRecognizerBase::kDefaultSwipeGestureTimeout);
}

// Tests down swipe detection case.
TEST_F(DownSwipeRecognizerTest, WonAfterGestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {0, .7f}));

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_FALSE(gesture_won());

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {0, .7f}});

  EXPECT_FALSE(member);
  EXPECT_TRUE(member.IsAcceptCalled());
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_TRUE(gesture_won());

  // Wait for the timeout, to make sure the scheduled task doesn't execute and crash us.
  RunLoopFor(a11y::SwipeRecognizerBase::kDefaultSwipeGestureTimeout);
}

// Tests right swipe detection case.
TEST_F(RightSwipeRecognizerTest, WonAfterGestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {.7f, 0}));

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_FALSE(gesture_won());

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {.7f, 0}});

  EXPECT_FALSE(member);
  EXPECT_TRUE(member.IsAcceptCalled());
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_TRUE(gesture_won());

  // Wait for the timeout, to make sure the scheduled task doesn't execute and crash us.
  RunLoopFor(a11y::SwipeRecognizerBase::kDefaultSwipeGestureTimeout);
}

// Tests left swipe detection case.
TEST_F(LeftSwipeRecognizerTest, WonAfterGestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {-.7f, 0}));

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_FALSE(gesture_won());

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {-.7f, 0}});

  EXPECT_FALSE(member);
  EXPECT_TRUE(member.IsAcceptCalled());
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_TRUE(gesture_won());

  // Wait for the timeout, to make sure the scheduled task doesn't execute and crash us.
  RunLoopFor(a11y::SwipeRecognizerBase::kDefaultSwipeGestureTimeout);
}

// Tests rejection case in which swipe gesture does not cover long enough distance.
TEST_F(UpSwipeRecognizerTest, RejectWhenDistanceTooSmall) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {0, -.2f}));

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {0, -.2f}});

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests rejection case in which swipe gesture covers too large a distance.
TEST_F(UpSwipeRecognizerTest, RejectWhenDistanceTooLarge) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {0, -1}});

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests rejection case in which swipe gesture exceeds timeout.
TEST_F(UpSwipeRecognizerTest, RejectWhenTimeoutExceeded) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  RunLoopFor(a11y::SwipeRecognizerBase::kDefaultSwipeGestureTimeout);

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests rejection case for upward swipe in which up gesture ends too far from vertical.
TEST_F(UpSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {.5f, -.5f}});

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests rejection case for upward swipe in which gesture takes invalid path.
TEST_F(UpSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {0, .1f}});

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests rejection case for downward swipe in which gesture ends in an invalid location.
TEST_F(DownSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {-.5f, .5f}});

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests rejection case for downward swipe in which gesture takes invalid path.
TEST_F(DownSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {0, -.1f}});

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests rejection case for right swipe in which gesture ends in an invalid location.
TEST_F(RightSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {.5f, .5f}});

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests rejection case for right swipe in which gesture takes invalid path.
TEST_F(RightSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {-.1f, 0}});

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests rejection case for left swipe in which gesture ends in an invalid location.
TEST_F(LeftSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {-.5f, -.5f}});

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests rejection case for left swipe in which gesture takes invalid path.
TEST_F(LeftSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {.1f, 0}});

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests Gesture Detection failure when multiple fingers are detected.
TEST_F(LeftSwipeRecognizerTest, MultiFingerDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // New pointer ID added, but it did not make contact with the screen yet.
  SendPointerEvent({2, Phase::ADD, {}});
  EXPECT_FALSE(member.IsRejectCalled());

  // Sends a down event with the second pointer ID, causing the gesture to be rejected.
  SendPointerEvent({2, Phase::DOWN, {}});
  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_won());
}

// Tests right swipe detection after member is declared winner.
TEST_F(RightSwipeRecognizerTest, RecognizeAfterWin) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // Calling OnWin() before gesture is recognized should not affect state.
  recognizer()->OnWin();
  EXPECT_TRUE(member);
  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_FALSE(gesture_won());

  SendPointerEvent({1, Phase::UP, {.5f, 0}});
  EXPECT_FALSE(member);
  EXPECT_TRUE(member.IsAcceptCalled());
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_TRUE(gesture_won());
}

// Tests right swipe loss.
TEST_F(RightSwipeRecognizerTest, Loss) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // Calling OnDefeat() before gesture is recognized abandons the gesture.
  recognizer()->OnDefeat();
  EXPECT_FALSE(member);
  EXPECT_FALSE(member.IsAcceptCalled());

  // Wait for the timeout, to make sure the scheduled task doesn't execute and crash us.
  RunLoopFor(a11y::SwipeRecognizerBase::kDefaultSwipeGestureTimeout);

  EXPECT_FALSE(gesture_won());
}

}  // namespace accessibility_test
