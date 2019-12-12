// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_tap_recognizer.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

namespace a11y {

struct OneFingerTapRecognizer::Contest {
  Contest(std::unique_ptr<ContestMember> contest_member)
      : member(std::move(contest_member)), long_press_timeout(member.get()) {}

  std::unique_ptr<ContestMember> member;
  // Indicates that a down event has been detected.
  bool in_progress = false;
  // Async task used to schedule long-press timeout.
  async::TaskClosureMethod<ContestMember, &ContestMember::Reject> long_press_timeout;
};

OneFingerTapRecognizer::OneFingerTapRecognizer(OnOneFingerTap callback, zx::duration tap_timeout)
    : one_finger_tap_callback_(std::move(callback)), tap_timeout_(tap_timeout) {}

OneFingerTapRecognizer::~OneFingerTapRecognizer() = default;

void OneFingerTapRecognizer::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_DCHECK(contest_);
  FX_DCHECK(pointer_event.has_phase());

  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      if (!InitGestureInfo(pointer_event, &gesture_start_info_, &gesture_context_)) {
        FX_LOGS(ERROR) << "Pointer Event is missing required fields. Dropping current event.";
        contest_.reset();
      } else if (contest_->in_progress ||
                 !ValidatePointerEvent(gesture_start_info_, pointer_event) ||
                 !ValidatePointerEventForTap(pointer_event)) {
        contest_.reset();
      } else {
        // Posts a timeout to catch long presses. If the gesture is performed before it executes,
        // the task is canceled.
        contest_->long_press_timeout.PostDelayed(async_get_default_dispatcher(), tap_timeout_);
        contest_->in_progress = true;
      }
      break;
    case fuchsia::ui::input::PointerEventPhase::MOVE:
      FX_DCHECK(contest_->in_progress)
          << "Pointer MOVE event received without preceding DOWN event.";

      if (!(ValidatePointerEvent(gesture_start_info_, pointer_event) &&
            ValidatePointerEventForTap(pointer_event))) {
        contest_.reset();
      }
      break;
    case fuchsia::ui::input::PointerEventPhase::UP:
      FX_DCHECK(contest_->in_progress) << "Pointer UP event received without preceding DOWN event.";

      if (ValidatePointerEvent(gesture_start_info_, pointer_event) &&
          ValidatePointerEventForTap(pointer_event)) {
        contest_->member->Accept();
      }
      contest_.reset();
      break;
    default:
      break;
  }
}

void OneFingerTapRecognizer::OnWin() { one_finger_tap_callback_(gesture_context_); }

void OneFingerTapRecognizer::OnDefeat() { contest_.reset(); }

bool OneFingerTapRecognizer::ValidatePointerEventForTap(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // Check if the new pointer event is under the threshold value for the move.
  auto dx = pointer_event.ndc_point().x - gesture_start_info_.starting_ndc_position.x;
  auto dy = pointer_event.ndc_point().y - gesture_start_info_.starting_ndc_position.y;
  return dx * dx + dy * dy <= kGestureMoveThreshold * kGestureMoveThreshold;
}

void OneFingerTapRecognizer::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  ResetGestureInfo(&gesture_start_info_);
  ResetGestureContext(&gesture_context_);
  contest_ = std::make_unique<Contest>(std::move(contest_member));
}

std::string OneFingerTapRecognizer::DebugName() const { return "one_finger_tap_recognizer"; }

}  // namespace a11y
