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

OneFingerTapRecognizer::OneFingerTapRecognizer(OnOneFingerTap callback, uint64_t tap_timeout)
    : one_finger_tap_callback_(std::move(callback)), one_finger_tap_timeout_(tap_timeout) {
  ResetState();
}

void OneFingerTapRecognizer::AddArenaMember(ArenaMember* new_arena_member) {
  FXL_CHECK(new_arena_member);
  arena_member_ = new_arena_member;
}

void OneFingerTapRecognizer::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  if (!pointer_event.has_phase()) {
    FX_LOGS(ERROR) << "Pointer event is missing phase information.";
    return;
  }
  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::ADD:
      ResetState();
      gesture_state_ = kInProgress;
      break;

    case fuchsia::ui::input::PointerEventPhase::DOWN:
      if (!InitGestureInfo(pointer_event, &gesture_start_info_, &gesture_context_)) {
        FX_LOGS(ERROR) << "Pointer Event is missing required fields. Dropping current event.";
        AbandonGesture();
        ResetState();
        break;
      }

      if ((gesture_state_ != kInProgress) ||
          !ValidatePointerEvent(gesture_start_info_, pointer_event) ||
          !ValidatePointerEventForTap(pointer_event)) {
        AbandonGesture();
        ResetState();
        break;
      }

      // Schedule a task to declare defeat with a timeout equal to one_finger_tap_timeout.
      ScheduleCallbackTask(
          &gesture_task_,
          [this](async_dispatcher_t* dispatcher, async::Task* task, zx_status_t status) {
            if (status == ZX_OK) {
              AbandonGesture();
            }
          },
          one_finger_tap_timeout_);
      gesture_state_ = kDownFingerDetected;

      break;

    case fuchsia::ui::input::PointerEventPhase::MOVE:
      if ((gesture_state_ != kDownFingerDetected) ||
          !ValidatePointerEvent(gesture_start_info_, pointer_event) ||
          !ValidatePointerEventForTap(pointer_event)) {
        AbandonGesture();
        ResetState();
        break;
      }

      break;

    case fuchsia::ui::input::PointerEventPhase::UP:
      if ((gesture_state_ != kDownFingerDetected) ||
          !ValidatePointerEvent(gesture_start_info_, pointer_event) ||
          !ValidatePointerEventForTap(pointer_event)) {
        AbandonGesture();
        ResetState();
        break;
      }
      CancelCallbackTask(&gesture_task_);
      if (is_winner_) {
        // One Tap Gesture detected.
        ExecuteOnWin();
      } else {
        gesture_state_ = kGestureDetectedAndWaiting;
      }
      break;
    default:
      break;
  }
}

void OneFingerTapRecognizer::OnWin() {
  is_winner_ = true;
  if (gesture_state_ == kGestureDetectedAndWaiting) {
    ExecuteOnWin();
  }
}

void OneFingerTapRecognizer::ExecuteOnWin() {
  GestureContext new_gesture_context = gesture_context_;
  one_finger_tap_callback_(new_gesture_context);
  if (!arena_member_) {
    FX_LOGS(ERROR) << "Arena member is not initialized.";
    return;
  }
  arena_member_->StopRoutingPointerEvents(
      fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  ResetState();
}
void OneFingerTapRecognizer::OnDefeat() { ResetState(); }

void OneFingerTapRecognizer::AbandonGesture() {
  if (!arena_member_) {
    FX_LOGS(ERROR) << "Arena member is not initialized.";
    return;
  }
  if (is_winner_) {
    arena_member_->StopRoutingPointerEvents(
        fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  } else {
    arena_member_->DeclareDefeat();
  }
}

void OneFingerTapRecognizer::ResetState() {
  // Cancel any pending callback.
  CancelCallbackTask(&gesture_task_);

  // Reset GestureInfo.
  gesture_start_info_.gesture_start_time = 0;
  gesture_start_info_.starting_global_position.x = 0;
  gesture_start_info_.starting_global_position.y = 0;
  // Important! Do not set local coordinates to zero. std::optional is used here
  // to indicate that they can be either present or not. Zero is present and may
  // lead to errors.
  gesture_start_info_.starting_local_position.reset();
  gesture_start_info_.device_id = 0;
  gesture_start_info_.pointer_id = 0;
  gesture_start_info_.view_ref_koid = ZX_KOID_INVALID;

  // Reset GestureState.
  gesture_state_ = kNotStarted;

  // Reset Gesture Context.
  gesture_context_.view_ref_koid = 0;
  gesture_context_.local_point->x = 0;
  gesture_context_.local_point->y = 0;

  // Reset is_winner_ flag.
  is_winner_ = false;
}

bool OneFingerTapRecognizer::ValidatePointerEventForTap(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // Check if the new pointer event is under the threshold value for the move.
  if ((std::abs(pointer_event.global_point().x - gesture_start_info_.starting_global_position.x) >
       kGestureMoveThreshold) ||
      (std::abs(pointer_event.global_point().y - gesture_start_info_.starting_global_position.y) >
       kGestureMoveThreshold)) {
    FX_LOGS(INFO) << "Touch point has moved more than the threshold value.";
    return false;
  }

  // Check if the new pointer event time(nano second) is within one finger tap timeout(milli
  // second).
  return (pointer_event.event_time() - gesture_start_info_.gesture_start_time) <=
         (one_finger_tap_timeout_ * 1000);
}

std::string OneFingerTapRecognizer::DebugName() { return "one_finger_tap_recognizer"; }

}  // namespace a11y
