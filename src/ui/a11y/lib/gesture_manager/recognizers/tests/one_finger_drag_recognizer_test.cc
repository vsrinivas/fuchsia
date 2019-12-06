// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_drag_recognizer.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <memory>

#include "gtest/gtest.h"
#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"

namespace accessibility_test {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

class OneFingerDragRecognizerTest : public gtest::TestLoopFixture {
 public:
  OneFingerDragRecognizerTest()
      : recognizer_(
            [this](a11y::GestureContext context) { gesture_updates_.push_back(context); },
            [this](a11y::GestureContext context) { gesture_cancel_callback_called_ = true; },
            [this](a11y::GestureContext context) { gesture_complete_callback_called_ = true; },
            a11y::OneFingerDragRecognizer::kDefaultMinDragDuration) {}

  void SendPointerEvents(const std::vector<PointerParams>& events) {
    for (const auto& event : events) {
      SendPointerEvent(event);
    }
  }

  void SendPointerEvent(const PointerParams& event) {
    recognizer_.HandleEvent(ToPointerEvent(event, 0));
  }

 protected:
  a11y::OneFingerDragRecognizer recognizer_;
  std::vector<a11y::GestureContext> gesture_updates_;
  bool gesture_cancel_callback_called_ = false;
  bool gesture_complete_callback_called_ = false;
};

// Tests successful drag detection case.
TEST_F(OneFingerDragRecognizerTest, WonAfterGestureDetected) {
  MockContestMember member;
  recognizer_.OnContestStarted(member.TakeInterface());

  glm::vec2 first_update_ndc_position = {0, .7f};
  auto first_update_local_coordinates = ToLocalCoordinates(first_update_ndc_position);

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, first_update_ndc_position));

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(gesture_updates_.empty());
  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);

  // Wait for the drag delay to elapse, at which point the recognizer should claim the win and
  // invoke the update callback.
  RunLoopFor(a11y::OneFingerDragRecognizer::kDefaultMinDragDuration);

  EXPECT_TRUE(member.IsAcceptCalled());
  // MockContestMember::Accept() does NOT call OnWin(), so we need to call manually since
  // ContestMember::Accept() promises to call this method.
  recognizer_.OnWin();

  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);

  // We should see an update at location of the last event ingested prior to the delay elapsing.
  EXPECT_EQ(gesture_updates_.size(), 1u);
  EXPECT_EQ(gesture_updates_[0].local_point->x, first_update_local_coordinates.x);
  EXPECT_EQ(gesture_updates_[0].local_point->y, first_update_local_coordinates.y);

  SendPointerEvents(MoveEvents(1, {0, .7f}, {0, .85f}) + UpEvents(1, {0, .85f}));

  EXPECT_FALSE(member);
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_TRUE(gesture_complete_callback_called_);

  // Since MoveEvents() generates 10 evenly-spaced
  // pointer events between the starting point (0, .7) and ending point (0, .85), the recognizer
  // will receive a series of MOVE events at (0, .715), (0, .73), ..., (0, .85). The first for which
  // the distance covered since the initial update, which occurred at (0, .7), will be the event
  // at (0, .775). We therefore expect an update to occur at this point. We would expect an
  // additional update when the distance between the pointer and (0, .775) exceeds .0625, which will
  // occur at (0, .85).
  auto second_update_local_coordinates = ToLocalCoordinates({0, .775f});
  auto third_update_local_coordinates = ToLocalCoordinates({0, .85f});

  EXPECT_EQ(gesture_updates_.size(), 3u);
  EXPECT_EQ(gesture_updates_[1].local_point->x, second_update_local_coordinates.x);
  EXPECT_EQ(gesture_updates_[1].local_point->y, second_update_local_coordinates.y);
  EXPECT_EQ(gesture_updates_[2].local_point->x, third_update_local_coordinates.x);
  EXPECT_EQ(gesture_updates_[2].local_point->y, third_update_local_coordinates.y);
}

// Verifies that recognizer rejects gesture after multiple down events.
TEST_F(OneFingerDragRecognizerTest, RejectAfterMultipleDownEvents) {
  MockContestMember member;
  recognizer_.OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {0, .7f}));

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(gesture_updates_.empty());
  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);

  // Wait for the drag delay to elapse, at which point the recognizer should claim the win and
  // invoke the update callback.
  RunLoopFor(a11y::OneFingerDragRecognizer::kDefaultMinDragDuration);

  EXPECT_TRUE(member.IsAcceptCalled());
  // MockContestMember::Accept() does NOT call OnWin(), so we need to call manually since
  // ContestMember::Accept() promises to call this method.
  recognizer_.OnWin();

  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);

  SendPointerEvents(DownEvents(1, {}));

  EXPECT_TRUE(member.IsRejectCalled());
  // MockContestMember::Reject() does NOT call OnDefeat(), so we need to call manually since
  // ContestMember::Reject() promises to call this method.
  recognizer_.OnDefeat();

  EXPECT_TRUE(member.IsAcceptCalled());
  EXPECT_FALSE(gesture_updates_.empty());
  EXPECT_TRUE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);
}

// Tests that distance threshold between updates is enforced after first update.
TEST_F(OneFingerDragRecognizerTest, MinimumDistanceRequirementForUpdatesEnforced) {
  MockContestMember member;
  recognizer_.OnContestStarted(member.TakeInterface());

  glm::vec2 update_ndc_position = {0, .7f};
  auto local_coordinates = ToLocalCoordinates(update_ndc_position);

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, update_ndc_position));

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(gesture_updates_.empty());
  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);

  // Wait for the drag delay to elapse, at which point the recognizer should claim the win and
  // invoke the update callback.
  RunLoopFor(a11y::OneFingerDragRecognizer::kDefaultMinDragDuration);

  EXPECT_TRUE(member.IsAcceptCalled());
  // MockContestMember::Accept() does NOT call OnWin(), so we need to call manually since
  // ContestMember::Accept() promises to call this method.
  recognizer_.OnWin();

  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);

  // We should see an update at location of the last event ingested prior to the delay elapsing.
  EXPECT_EQ(gesture_updates_.size(), 1u);
  EXPECT_EQ(gesture_updates_[0].local_point->x, local_coordinates.x);
  EXPECT_EQ(gesture_updates_[0].local_point->y, local_coordinates.y);

  // Move pointer to location that does NOT meet the minimum threshold update.
  SendPointerEvents(MoveEvents(1, {0, .7f}, {0, .75f}) + UpEvents(1, {0, .75f}));

  EXPECT_FALSE(member);
  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_TRUE(gesture_complete_callback_called_);

  // The update callback should only be invoked again if the pointer moves a sufficient distance
  // from the previous update. Since the pointer only moves .05f in this case, and the threshold
  // for an update is 1.f/16, no further updates should have occurred.
  EXPECT_EQ(gesture_updates_.size(), 1u);
}

// Verifies that recognizer does not accept gesture before delay period elapses.
TEST_F(OneFingerDragRecognizerTest, DoNotAcceptPriorToDelayElapsing) {
  MockContestMember member;
  recognizer_.OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {0, .7f}));

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(gesture_updates_.empty());
  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);

  SendPointerEvents(UpEvents(1, {0, .7f}));

  EXPECT_FALSE(member.IsRejectCalled());
  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(gesture_updates_.empty());
  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);

  // Wait for the drag delay to elapse to ensure that task scheduled to claim win was cancelled.
  // The task calls Accept(), and then invokes the drag update callback. Therefore, if it was
  // cancelled successfully, we would not expect either method to have been called.
  RunLoopFor(a11y::OneFingerDragRecognizer::kDefaultMinDragDuration);

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(gesture_updates_.empty());
}

// Tests gesture detection failure when multiple fingers are detected.
TEST_F(OneFingerDragRecognizerTest, MultiFingerDetected) {
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
  // MockContestMember::Reject() does NOT call OnDefeat(), so we need to call manually since
  // ContestMember::Reject() promises to call this method.
  recognizer_.OnDefeat();

  EXPECT_TRUE(gesture_updates_.empty());
  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);
}

// Tests that recognizer abandons gesture if call to Accept() fails.
TEST_F(OneFingerDragRecognizerTest, AbandonGestureOnAcceptFailure) {
  MockContestMember member;
  member.SetAccept(false);
  recognizer_.OnContestStarted(member.TakeInterface());

  SendPointerEvents(DownEvents(1, {}) + MoveEvents(1, {}, {0, .7f}));

  EXPECT_FALSE(member.IsAcceptCalled());
  EXPECT_TRUE(gesture_updates_.empty());
  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);

  // Wait for the drag delay to elapse, at which point the recognizer should attempt to claim the
  // win. Since Accept() will return false, the recognizer should NOT call the update task, and
  // should instead abandon the gesture.
  RunLoopFor(a11y::OneFingerDragRecognizer::kDefaultMinDragDuration);

  EXPECT_TRUE(member.IsAcceptCalled());
  EXPECT_TRUE(member.IsRejectCalled());
  // MockContestMember::Reject() does NOT call OnDefeat(), so we need to call manually since
  // ContestMember::Reject() promises to call this method.
  recognizer_.OnDefeat();

  EXPECT_TRUE(gesture_updates_.empty());
  EXPECT_FALSE(gesture_cancel_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);
}

}  // namespace accessibility_test
