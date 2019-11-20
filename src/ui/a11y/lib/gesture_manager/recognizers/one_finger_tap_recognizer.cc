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
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      if (!InitGestureInfo(pointer_event, &gesture_start_info_, &gesture_context_)) {
        FX_LOGS(ERROR) << "Pointer Event is missing required fields. Dropping current event.";
        AbandonGesture();
      } else if (in_progress_ || !ValidatePointerEvent(gesture_start_info_, pointer_event) ||
                 !ValidatePointerEventForTap(pointer_event)) {
        AbandonGesture();
      } else {
        // Posts a timeout to catch long presses. If the gesture is performed before it executes,
        // the task is canceled.
        abandon_task_.PostDelayed(async_get_default_dispatcher(), tap_timeout_);
        in_progress_ = true;
      }
      break;
    case fuchsia::ui::input::PointerEventPhase::MOVE:
      if (!in_progress_) {
        FX_LOGS(ERROR) << "Pointer MOVE event received without preceding DOWN event.";
      } else if (!(ValidatePointerEvent(gesture_start_info_, pointer_event) &&
                   ValidatePointerEventForTap(pointer_event))) {
        AbandonGesture();
      }
      break;
    case fuchsia::ui::input::PointerEventPhase::UP:
      if (!in_progress_) {
        FX_LOGS(ERROR) << "Pointer UP event received without preceding DOWN event.";
      } else if (!(ValidatePointerEvent(gesture_start_info_, pointer_event) &&
                   ValidatePointerEventForTap(pointer_event))) {
        AbandonGesture();
      } else {
        // The gesture was detected.

        abandon_task_.Cancel();

        // If we'd already won by default, we need to handle it now.
        if (contest_member_->status() == ContestMember::Status::kWinner) {
          ExecuteOnWin();
        }
        // As this is a passive gesture, it never forces the win on the arena. It waits to win by
        // the last standing rule. Release the member to become passive.
        contest_member_.reset();
      }
      break;
    default:
      break;
  }
}

void OneFingerTapRecognizer::OnWin() {
  // If we've received the win after having released the contest member, we're in passive
  // waiting-for-the-win mode.
  if (!contest_member_) {
    ExecuteOnWin();
  }
  // If we haven't released the contest member, we're still verifying the gesture.
}

void OneFingerTapRecognizer::ExecuteOnWin() { one_finger_tap_callback_(gesture_context_); }

void OneFingerTapRecognizer::OnDefeat() {
  abandon_task_.Cancel();
  contest_member_.reset();
}

void OneFingerTapRecognizer::AbandonGesture() {
  FX_DCHECK(contest_member_) << "No active contest.";

  contest_member_->Reject();
  // Remaining cleanup happens in |OnDefeat|.
}

void OneFingerTapRecognizer::ResetState() {
  ResetGestureInfo(&gesture_start_info_);
  in_progress_ = false;
  ResetGestureContext(&gesture_context_);
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

void OneFingerTapRecognizer::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  ResetState();
  contest_member_ = std::move(contest_member);
}

std::string OneFingerTapRecognizer::DebugName() const { return "one_finger_tap_recognizer"; }

}  // namespace a11y
