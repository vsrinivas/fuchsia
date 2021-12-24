// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/two_finger_drag_recognizer.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"
#include "src/ui/a11y/lib/testing/input.h"

#include <glm/glm.hpp>

namespace accessibility_test {
namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

class TwoFingerDragRecognizerTest : public gtest::TestLoopFixture {
 public:
  TwoFingerDragRecognizerTest() {}

  void SendPointerEvents(const std::vector<PointerParams>& events) {
    for (const auto& event : events) {
      SendPointerEvent(event);
    }
  }

  void SendPointerEvent(const PointerParams& event) {
    if (member_.is_held()) {
      recognizer_->HandleEvent(ToPointerEvent(event, 0));
    }
  }

  void CreateGestureRecognizer() {
    recognizer_ = std::make_unique<a11y::TwoFingerDragRecognizer>(
        [this](a11y::GestureContext context) { gesture_start_callback_called_ = true; },
        [this](a11y::GestureContext context) { gesture_updates_.push_back(context); },
        [this](a11y::GestureContext context) { gesture_complete_callback_called_ = true; },
        a11y::TwoFingerDragRecognizer::kDefaultMinDragDuration);

    recognizer_->OnContestStarted(member_.TakeInterface());
  }

 protected:
  MockContestMember member_;
  std::unique_ptr<a11y::TwoFingerDragRecognizer> recognizer_;
  std::vector<a11y::GestureContext> gesture_updates_;
  bool gesture_start_callback_called_ = false;
  bool gesture_complete_callback_called_ = false;
};

// Tests successful drag detection case where time threshold is exceeded.
TEST_F(TwoFingerDragRecognizerTest, WonAfterGestureDetectedTimeThreshold) {
  CreateGestureRecognizer();

  glm::vec2 first_update_ndc_position = {0, .01f};
  auto first_update_local_coordinates = ToLocalCoordinates(first_update_ndc_position);

  SendPointerEvents(DownEvents(1, {0, 0.01f}) + DownEvents(2, {}) + MoveEvents(1, {}, {0, .01f}));

  // Wait for the drag delay to elapse, at which point the recognizer should claim the win and
  // invoke the update callback.
  RunLoopFor(a11y::TwoFingerDragRecognizer::kDefaultMinDragDuration);

  ASSERT_EQ(member_.status(), a11y::ContestMember::Status::kAccepted);
  recognizer_->OnWin();

  EXPECT_TRUE(gesture_start_callback_called_);
  EXPECT_FALSE(gesture_complete_callback_called_);

  // We should see an update at location of the last event ingested prior to the delay elapsing.
  EXPECT_EQ(gesture_updates_.size(), 1u);
  {
    auto& location = gesture_updates_[0].current_pointer_locations[1].local_point;
    EXPECT_EQ(location.x, first_update_local_coordinates.x);
    EXPECT_EQ(location.y, first_update_local_coordinates.y);
  }
  {
    auto& location = gesture_updates_[0].current_pointer_locations[2].local_point;
    EXPECT_EQ(location.x, 0);
    EXPECT_EQ(location.y, 0);
  }

  SendPointerEvents(MoveEvents(2, {0, 0}, {0, .1}) + UpEvents(2, {0, .1}));

  EXPECT_FALSE(member_.is_held());
  EXPECT_TRUE(gesture_complete_callback_called_);

  // Since MoveEvents() generates 10 evenly-spaced pointer events between the starting point (0, 0)
  // and ending point (0, .1). We should receive upadates for each event.

  EXPECT_EQ(gesture_updates_.size(), 11u);
  {
    auto& location = gesture_updates_[10].current_pointer_locations[2].ndc_point;
    EXPECT_EQ(location.x, 0);
    EXPECT_GT(location.y, 0.09f);
    EXPECT_LT(location.y, 0.11f);
  }
}

// Drag detected after separation threshold exceeded.
TEST_F(TwoFingerDragRecognizerTest, WonAfterGestureDetectedSeparationThresholdIncreasing) {
  CreateGestureRecognizer();

  SendPointerEvents(DownEvents(1, {0, 0}) + DownEvents(2, {0, 0.01}) +
                    MoveEvents(1, {}, {0, .02f}));

  // Once the distance between the two pointers has increased or decreased by
  // more than 20%, we should accept.
  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kAccepted);
}

// Drag detected after separation threshold exceeded.
TEST_F(TwoFingerDragRecognizerTest, WonAfterGestureDetectedSeparationThresholdDecreasing) {
  CreateGestureRecognizer();

  SendPointerEvents(DownEvents(1, {0, 0}) + DownEvents(2, {0, 0.05}) +
                    MoveEvents(1, {}, {0, .02f}));

  // Once the distance between the two pointers has increased or decreased by
  // more than 20%, we should accept.
  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kAccepted);
}

// Drag detected after displacement threshold exceeded.
TEST_F(TwoFingerDragRecognizerTest, WonAfterGestureDetectedDisplacementThreshold) {
  CreateGestureRecognizer();

  SendPointerEvents(DownEvents(1, {0, 0}) + DownEvents(2, {0, 0.5}) +
                    MoveEvents(2, {0, 0.5}, {0, .59f}));

  // The cenroid has not yet moved by .1f, so remain undecided.
  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kUndecided);

  SendPointerEvents(MoveEvents(1, {0, 0}, {0, .12}));

  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kAccepted);
}

// Drag rejected when third finger comes down.
TEST_F(TwoFingerDragRecognizerTest, RejectTooManyFingers) {
  CreateGestureRecognizer();

  SendPointerEvents(DownEvents(1, {0, 0}) + DownEvents(2, {0, 0.5}) + DownEvents(3, {0, 0}));

  // The cenroid has not yet moved by .1f, so remain undecided.
  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kRejected);
}

// Drag rejected if the second finger doesn't come down soon enough.
TEST_F(TwoFingerDragRecognizerTest, RejectSecondFingerTimeout) {
  CreateGestureRecognizer();

  SendPointerEvents(DownEvents(1, {0, 0}));

  RunLoopFor(a11y::TwoFingerDragRecognizer::kDefaultMinDragDuration);

  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kRejected);
}

// Drag rejected if we see an UP event before the second DOWN event.
TEST_F(TwoFingerDragRecognizerTest, RejectFirstFingerLiftedBeforeSecondFingerDown) {
  CreateGestureRecognizer();

  SendPointerEvents(DownEvents(1, {0, 0}) + UpEvents(1, {0, 0}));

  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kRejected);
}

// Drag accepted only after the second finger comes down, even if the
// displacement for the first finger is large.
TEST_F(TwoFingerDragRecognizerTest, OnlyCheckDisplacementIfTwoFingersDown) {
  CreateGestureRecognizer();

  SendPointerEvents(DownEvents(1, {0, 0}) + MoveEvents(1, {0, 0}, {0, 1}) + DownEvents(2, {0, 0}));

  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kUndecided);

  SendPointerEvents(MoveEvents(2, {0, 0}, {0, 0.6}));

  EXPECT_EQ(member_.status(), a11y::ContestMember::Status::kAccepted);
}

}  // namespace
}  // namespace accessibility_test
