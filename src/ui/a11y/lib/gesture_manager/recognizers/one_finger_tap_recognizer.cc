// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_tap_recognizer.h"

#include <lib/syslog/cpp/logger.h>

#include <valarray>

#include <src/lib/fxl/logging.h>

#include "lib/async/cpp/task.h"
#include "lib/async/default.h"
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

void OneFingerTapRecognizer::ScheduleCallbackTask() {
  gesture_task_.set_handler(
      [this](async_dispatcher_t* dispatcher, async::Task* task, zx_status_t status) {
        if (status == ZX_OK) {
          AbandonGesture();
        }
      });

  gesture_task_.PostDelayed(async_get_default_dispatcher(), zx::msec(one_finger_tap_timeout_));
}
void OneFingerTapRecognizer::CancelCallbackTask() { gesture_task_.Cancel(); }

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
      if (!InitGestureInfo(pointer_event)) {
        FX_LOGS(ERROR) << "Pointer Event is missing required fields. Dropping current event.";
        AbandonGesture();
        ResetState();
        break;
      }

      if ((gesture_state_ != kInProgress) || !ValidatePointerEvent(pointer_event)) {
        AbandonGesture();
        ResetState();
        break;
      }

      // Schedule a task to declare defeat with a timeout equal to one_finger_tap_timeout.
      ScheduleCallbackTask();
      gesture_state_ = kDownFingerDetected;

      break;

    case fuchsia::ui::input::PointerEventPhase::MOVE:
      if ((gesture_state_ != kDownFingerDetected) || !ValidatePointerEvent(pointer_event)) {
        AbandonGesture();
        ResetState();
        break;
      }

      break;

    case fuchsia::ui::input::PointerEventPhase::UP:
      if ((gesture_state_ != kDownFingerDetected) || !ValidatePointerEvent(pointer_event)) {
        AbandonGesture();
        ResetState();
        break;
      }
      CancelCallbackTask();
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
  CancelCallbackTask();

  // Reset GestureInfo.
  gesture_start_info_.gesture_start_time = 0;
  gesture_start_info_.starting_global_position.x = 0;
  gesture_start_info_.starting_global_position.y = 0;
  gesture_start_info_.starting_local_position.x = 0;
  gesture_start_info_.starting_local_position.y = 0;
  gesture_start_info_.device_id = 0;
  gesture_start_info_.pointer_id = 0;
  gesture_start_info_.view_ref_koid = ZX_KOID_INVALID;

  // Reset GestureState.
  gesture_state_ = kNotStarted;

  // Reset Gesture Context.
  gesture_context_.view_ref_koid = 0;
  gesture_context_.local_point.x = 0;
  gesture_context_.local_point.y = 0;

  // Reset is_winner_ flag.
  is_winner_ = false;
}

bool OneFingerTapRecognizer::InitGestureInfo(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  if (!pointer_event.has_event_time()) {
    return false;
  }
  gesture_start_info_.gesture_start_time = pointer_event.event_time();

  if (!pointer_event.has_pointer_id()) {
    return false;
  }
  gesture_start_info_.pointer_id = pointer_event.pointer_id();

  if (!pointer_event.has_device_id()) {
    return false;
  }
  gesture_start_info_.device_id = pointer_event.device_id();

  if (!pointer_event.has_global_point()) {
    return false;
  }
  gesture_start_info_.starting_global_position = pointer_event.global_point();

  if (!pointer_event.has_local_point()) {
    return false;
  }
  gesture_start_info_.starting_local_position = pointer_event.local_point();

  if (pointer_event.has_viewref_koid()) {
    gesture_start_info_.view_ref_koid = pointer_event.viewref_koid();
  }

  // Init GestureContext.
  gesture_context_.view_ref_koid = gesture_start_info_.view_ref_koid;
  gesture_context_.local_point = gesture_start_info_.starting_local_position;
  return true;
}

bool OneFingerTapRecognizer::ValidatePointerEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const {
  // Check if pointer_event has all the required fields.
  if (!pointer_event.has_event_time() || !pointer_event.has_pointer_id() ||
      !pointer_event.has_device_id() || !pointer_event.has_global_point() ||
      !pointer_event.has_local_point()) {
    FX_LOGS(ERROR) << "Pointer Event is missing required information.";
    return false;
  }

  // Check if pointer event information matches the gesture start information.
  if ((gesture_start_info_.device_id != pointer_event.device_id()) ||
      (gesture_start_info_.pointer_id != pointer_event.pointer_id())) {
    FX_LOGS(INFO) << "Pointer event is not valid for current gesture.";
    return false;
  }

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
