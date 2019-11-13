// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_tap_recognizer.h"

#include <valarray>

#include "lib/async/cpp/task.h"
#include "lib/async/default.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

namespace a11y {

OneFingerTapRecognizer::OneFingerTapRecognizer(OnOneFingerTap callback, zx::duration tap_timeout)
    : one_finger_tap_callback_(std::move(callback)),
      abandon_task_(this),
      tap_timeout_(tap_timeout) {}

void OneFingerTapRecognizer::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  if (!pointer_event.has_phase()) {
    FX_LOGS(ERROR) << "Pointer event is missing phase information.";
    return;
  }
  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::ADD:
      break;
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      if (!InitGestureInfo(pointer_event, &gesture_start_info_, &gesture_context_)) {
        FX_LOGS(ERROR) << "Pointer Event is missing required fields. Dropping current event.";
        AbandonGesture();
        break;
      }
      if ((gesture_state_ != TapGestureState::kNotStarted) ||
          !ValidatePointerEvent(gesture_start_info_, pointer_event) ||
          !ValidatePointerEventForTap(pointer_event)) {
        AbandonGesture();
        break;
      }
      // Posts a tasks. If the gesture is performed before it executes, the task is canceled.
      abandon_task_.PostDelayed(async_get_default_dispatcher(), tap_timeout_);
      gesture_state_ = TapGestureState::kDownFingerDetected;
      break;
    case fuchsia::ui::input::PointerEventPhase::MOVE:
      if ((gesture_state_ != TapGestureState::kDownFingerDetected) ||
          !ValidatePointerEvent(gesture_start_info_, pointer_event) ||
          !ValidatePointerEventForTap(pointer_event)) {
        AbandonGesture();
        break;
      }
      break;
    case fuchsia::ui::input::PointerEventPhase::UP:
      if ((gesture_state_ != TapGestureState::kDownFingerDetected) ||
          !ValidatePointerEvent(gesture_start_info_, pointer_event) ||
          !ValidatePointerEventForTap(pointer_event)) {
        AbandonGesture();
        break;
      }
      // The gesture was detected, cancels the task.
      abandon_task_.Cancel();

      // The gesture was detected. Please note as this is a passive gesture, it
      // never forces the win on the arena. It waits to win by the last standing
      // rule.
      gesture_state_ = TapGestureState::kGestureDetected;
      ExecuteOnWin();
      break;
    default:
      break;
  }
}

void OneFingerTapRecognizer::OnWin() {
  is_winner_ = true;
  if (gesture_state_ == TapGestureState::kGestureDetected) {
    ExecuteOnWin();
  }
}

void OneFingerTapRecognizer::ExecuteOnWin() {
  if (is_winner_) {
    one_finger_tap_callback_(gesture_context_);
    gesture_state_ = TapGestureState::kDone;
  }
}

void OneFingerTapRecognizer::OnDefeat() { gesture_state_ = TapGestureState::kDone; }

void OneFingerTapRecognizer::AbandonGesture() {
  if (!arena_member_) {
    FX_LOGS(ERROR) << "Arena member is not initialized.";
    return;
  }
  arena_member_->Reject();
  abandon_task_.Cancel();
  gesture_state_ = TapGestureState::kDone;
}

void OneFingerTapRecognizer::ResetState() {
  // Cancels any pending task.
  abandon_task_.Cancel();
  // Reset GestureInfo.
  gesture_start_info_.gesture_start_time = 0;
  gesture_start_info_.starting_ndc_position.x = 0;
  gesture_start_info_.starting_ndc_position.y = 0;
  // Important! Do not set local coordinates to zero. std::optional is used here
  // to indicate that they can be either present or not. Zero is present and may
  // lead to errors.
  gesture_start_info_.starting_local_position.reset();
  gesture_start_info_.device_id = 0;
  gesture_start_info_.pointer_id = 0;
  gesture_start_info_.view_ref_koid = ZX_KOID_INVALID;

  // Reset GestureState.
  gesture_state_ = TapGestureState::kNotStarted;

  // Reset Gesture Context.
  gesture_context_.view_ref_koid = 0;
  gesture_context_.local_point->x = 0;
  gesture_context_.local_point->y = 0;

  is_winner_ = false;
}

bool OneFingerTapRecognizer::ValidatePointerEventForTap(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // Check if the new pointer event is under the threshold value for the move.
  if ((std::abs(pointer_event.ndc_point().x - gesture_start_info_.starting_ndc_position.x) >
       kGestureMoveThreshold) ||
      (std::abs(pointer_event.ndc_point().y - gesture_start_info_.starting_ndc_position.y) >
       kGestureMoveThreshold)) {
    FX_LOGS(INFO) << "Touch point has moved more than the threshold value.";
    return false;
  }

  return true;
}

void OneFingerTapRecognizer::OnContestStarted() { ResetState(); }

std::string OneFingerTapRecognizer::DebugName() const { return "one_finger_tap_recognizer"; }

}  // namespace a11y
