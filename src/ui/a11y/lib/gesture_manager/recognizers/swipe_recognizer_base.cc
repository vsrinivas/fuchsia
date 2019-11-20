// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/swipe_recognizer_base.h"

#include <valarray>

#include "lib/async/cpp/task.h"
#include "lib/async/default.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

namespace a11y {

SwipeRecognizerBase::SwipeRecognizerBase(SwipeGestureCallback callback,
                                         zx::duration swipe_gesture_timeout)
    : swipe_gesture_callback_(std::move(callback)),
      abandon_task_(this),
      swipe_gesture_timeout_(swipe_gesture_timeout) {}

void SwipeRecognizerBase::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  if (!pointer_event.has_phase()) {
    FX_LOGS(ERROR) << "Pointer event is missing phase information.";
    return;
  }

  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      if (!InitGestureInfo(pointer_event, &gesture_start_info_, &gesture_context_)) {
        FX_LOGS(ERROR) << "Pointer Event is missing required fields. Dropping current event.";
        AbandonGesture();
      } else if (in_progress_ || !ValidatePointerEvent(gesture_start_info_, pointer_event)) {
        AbandonGesture();
      } else {
        // Schedule a task to declare defeat with a timeout equal to swipe_gesture_timeout_.
        abandon_task_.PostDelayed(async_get_default_dispatcher(), swipe_gesture_timeout_);
        in_progress_ = true;
      }
      break;
    case fuchsia::ui::input::PointerEventPhase::MOVE:
      if (!in_progress_) {
        FX_LOGS(ERROR) << "Pointer MOVE event received without preceding DOWN event.";
      } else if (!(ValidatePointerEvent(gesture_start_info_, pointer_event) &&
                   ValidateSwipePath(pointer_event))) {
        AbandonGesture();
      }
      break;
    case fuchsia::ui::input::PointerEventPhase::UP:
      if (!in_progress_) {
        FX_LOGS(ERROR) << "Pointer UP event received without preceding DOWN event.";
      } else if (!(ValidatePointerEvent(gesture_start_info_, pointer_event) &&
                   ValidateSwipePath(pointer_event) && ValidateSwipeDistance(pointer_event))) {
        AbandonGesture();
      } else {
        // CallbackTask is set to abandon gesture if timeout is exceeded, so we need to cancel
        // the callback if a gesture is detected within the timeout period.
        abandon_task_.Cancel();

        FX_DCHECK(contest_member_);
        contest_member_->Accept();
        swipe_gesture_callback_(gesture_context_);
        contest_member_.reset();
      }
      break;
    default:
      break;
  }
}

void SwipeRecognizerBase::OnDefeat() {
  abandon_task_.Cancel();
  contest_member_.reset();
}

void SwipeRecognizerBase::AbandonGesture() {
  FX_DCHECK(contest_member_) << "No active contest.";

  contest_member_->Reject();
  // Remaining cleanup  happens in |OnDefeat|.
}

void SwipeRecognizerBase::ResetState() {
  ResetGestureInfo(&gesture_start_info_);
  in_progress_ = false;
  ResetGestureContext(&gesture_context_);
}

bool SwipeRecognizerBase::ValidateSwipePath(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // Verify that slope of line containing gesture start point and current pointer event location
  // falls within pre-specified range.
  auto x_displacement = pointer_event.ndc_point().x - gesture_start_info_.starting_ndc_position.x;
  auto y_displacement = pointer_event.ndc_point().y - gesture_start_info_.starting_ndc_position.y;

  return ValidateSwipeSlopeAndDirection(x_displacement, y_displacement);
}

bool SwipeRecognizerBase::ValidateSwipeDistance(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // Check if the distance between the pointer event and the start point is within the allowable
  // range.
  auto x_displacement = pointer_event.ndc_point().x - gesture_start_info_.starting_ndc_position.x;
  auto y_displacement = pointer_event.ndc_point().y - gesture_start_info_.starting_ndc_position.y;

  auto swipe_distance = std::hypot(x_displacement, y_displacement);

  return (swipe_distance >= kMinSwipeDistance && swipe_distance <= kMaxSwipeDistance);
}

void SwipeRecognizerBase::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  ResetState();
  contest_member_ = std::move(contest_member);
}

}  // namespace a11y
