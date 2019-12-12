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

class TestSwipeRecognizer : public a11y::SwipeRecognizerBase {
 public:
  TestSwipeRecognizer(SwipeGestureCallback callback)
      : SwipeRecognizerBase(std::move(callback), SwipeRecognizerBase::kDefaultSwipeGestureTimeout) {
  }

  void set_valid(bool valid) { valid_ = valid; }

  std::string DebugName() const override { return "test_swipe_recognizer"; };

 private:
  bool ValidateSwipeSlopeAndDirection(float x_displacement, float y_displacement) override {
    return valid_;
  }

  bool valid_ = true;
};

template <typename Recognizer>
class SwipeRecognizerTest : public gtest::TestLoopFixture {
 public:
  SwipeRecognizerTest()
      : recognizer_([this](a11y::GestureContext context) {
          gesture_won_ = true;
          gesture_context_ = context;
        }) {}

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

TEST_F(SwipeRecognizerBaseTest, Win) {
  recognizer()->OnWin();
  EXPECT_TRUE(gesture_won());
}

TEST_F(SwipeRecognizerBaseTest, Defeat) {
  recognizer()->OnDefeat();
  EXPECT_FALSE(gesture_won());
}

// Ensures that the test recognier, which considers all swipe paths valid by default, calls |Accept|
// on |UP|. The base recognizer still validates swipe distance.
TEST_F(SwipeRecognizerBaseTest, Accept) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  ASSERT_TRUE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);

  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {0, .7f}});

  EXPECT_FALSE(member.is_held());
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

// Tests rejection case in which swipe gesture does not cover long enough distance.
TEST_F(SwipeRecognizerBaseTest, RejectWhenDistanceTooSmall) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {0, .2f}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case in which swipe gesture covers too large a distance.
TEST_F(SwipeRecognizerBaseTest, RejectWhenDistanceTooLarge) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {0, 1}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

TEST_F(SwipeRecognizerBaseTest, Timeout) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  RunLoopFor(a11y::SwipeRecognizerBase::kDefaultSwipeGestureTimeout);
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

TEST_F(SwipeRecognizerBaseTest, NoTimeoutAfterDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {0, .7f}});

  // By now, the member has been released (verified in the |Accept| test), so state can no longer
  // change. Wait for the timeout, to make sure the scheduled task doesn't execute and crash us.
  RunLoopFor(a11y::SwipeRecognizerBase::kDefaultSwipeGestureTimeout);
}

// Tests Gesture Detection failure when multiple fingers are detected.
TEST_F(SwipeRecognizerBaseTest, RejectMultiFinger) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));

  // New pointer ID added, but it did not make contact with the screen yet.
  SendPointerEvent({2, Phase::ADD, {}});
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kUndecided);

  // Sends a down event with the second pointer ID, causing the gesture to be rejected.
  SendPointerEvent({2, Phase::DOWN, {}});
  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

TEST_F(UpSwipeRecognizerTest, GestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {0, -.7f}));
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {0, -.7f}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

TEST_F(DownSwipeRecognizerTest, GestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {0, .7f}));
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {0, .7f}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

TEST_F(RightSwipeRecognizerTest, GestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {.7f, 0}));
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {.7f, 0}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

TEST_F(LeftSwipeRecognizerTest, GestureDetected) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {-.7f, 0}));
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  SendPointerEvent({1, Phase::UP, {-.7f, 0}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kAccepted);
}

// Tests rejection case for upward swipe in which up gesture ends too far from vertical.
TEST_F(UpSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::UP, {.5f, -.5f}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for upward swipe in which gesture takes invalid path.
TEST_F(UpSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {0, .1f}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for downward swipe in which gesture ends in an invalid location.
TEST_F(DownSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::UP, {-.5f, .5f}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for downward swipe in which gesture takes invalid path.
TEST_F(DownSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {0, -.1f}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for right swipe in which gesture ends in an invalid location.
TEST_F(RightSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::UP, {.5f, .5f}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for right swipe in which gesture takes invalid path.
TEST_F(RightSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {-.1f, 0}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for left swipe in which gesture ends in an invalid location.
TEST_F(LeftSwipeRecognizerTest, RejectSwipeOnInvalidEndLocation) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::UP, {-.5f, -.5f}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

// Tests rejection case for left swipe in which gesture takes invalid path.
TEST_F(LeftSwipeRecognizerTest, RejectSwipeOnInvalidPath) {
  MockContestMember member;
  recognizer()->OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}));
  SendPointerEvent({1, Phase::MOVE, {.1f, 0}});

  EXPECT_EQ(member.status(), a11y::ContestMember::Status::kRejected);
}

}  // namespace
}  // namespace accessibility_test
