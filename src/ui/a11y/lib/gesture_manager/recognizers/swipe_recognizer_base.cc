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

void SwipeRecognizerBase::AddArenaMember(ArenaMember* new_arena_member) {
  FXL_CHECK(new_arena_member);
  arena_member_ = new_arena_member;
}

void SwipeRecognizerBase::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  if (!pointer_event.has_phase()) {
    FX_LOGS(ERROR) << "Pointer event is missing phase information.";
    return;
  }

  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::ADD:
      break;

    case fuchsia::ui::input::PointerEventPhase::DOWN:
      if ((gesture_state_ != SwipeGestureState::kNotStarted)) {
        AbandonGesture();
        break;
      }

      if (!InitGestureInfo(pointer_event, &gesture_start_info_, &gesture_context_)) {
        FX_LOGS(ERROR) << "Pointer Event is missing required fields. Dropping current event.";
        AbandonGesture();
        break;
      }

      if (!ValidatePointerEvent(gesture_start_info_, pointer_event)) {
        AbandonGesture();
        break;
      }

      // Schedule a task to declare defeat with a timeout equal to swipe_gesture_timeout_.
      abandon_task_.PostDelayed(async_get_default_dispatcher(), swipe_gesture_timeout_);
      gesture_state_ = SwipeGestureState::kDownFingerDetected;

      break;

    case fuchsia::ui::input::PointerEventPhase::MOVE:
      if ((gesture_state_ != SwipeGestureState::kDownFingerDetected) ||
          !ValidatePointerEvent(gesture_start_info_, pointer_event) ||
          !ValidateSwipePath(pointer_event)) {
        AbandonGesture();
        break;
      }

      break;

    case fuchsia::ui::input::PointerEventPhase::UP:
      if ((gesture_state_ != SwipeGestureState::kDownFingerDetected) ||
          !ValidatePointerEvent(gesture_start_info_, pointer_event) ||
          !ValidateSwipePath(pointer_event) || !ValidateSwipeDistance(pointer_event)) {
        AbandonGesture();
        break;
      }

      // CallbackTask is set to abandon gesture if timeout is exceeded, so we need to cancel
      // the callback if a gesture is detected within the timeout period.
      abandon_task_.Cancel();

      gesture_state_ = SwipeGestureState::kGestureDetected;

      // Attempt to claim the win (if not already the winner).
      // If unsuccessful, abandon gesture and reset state.
      // If successful, arena_member_->Accept() will call OnWin(), so no further work is required.
      if (is_winner_) {
        ExecuteOnWin();
      } else if (!arena_member_->Accept()) {
        AbandonGesture();
      }

      break;

    default:
      break;
  }
}

void SwipeRecognizerBase::OnWin() {
  is_winner_ = true;
  if (gesture_state_ == SwipeGestureState::kGestureDetected) {
    ExecuteOnWin();
  }
}

void SwipeRecognizerBase::ExecuteOnWin() {
  if (is_winner_) {
    swipe_gesture_callback_(gesture_context_);
    gesture_state_ = SwipeGestureState::kDone;
  }
}
void SwipeRecognizerBase::OnDefeat() { gesture_state_ = SwipeGestureState::kDone; }

void SwipeRecognizerBase::AbandonGesture() {
  if (!arena_member_) {
    FX_LOGS(ERROR) << "Arena member is not initialized.";
    return;
  }

  arena_member_->Reject();
  abandon_task_.Cancel();
  ResetState();
}

void SwipeRecognizerBase::ResetState() {
  abandon_task_.Cancel();

  // Reset gesture start info.
  ResetGestureInfo(&gesture_start_info_);

  // Reset gesture state.
  gesture_state_ = SwipeGestureState::kNotStarted;

  // Reset gesture context.
  ResetGestureContext(&gesture_context_);

  // Reset is_winner_ flag.
  is_winner_ = false;
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

void SwipeRecognizerBase::OnContestStarted() { ResetState(); }

}  // namespace a11y
