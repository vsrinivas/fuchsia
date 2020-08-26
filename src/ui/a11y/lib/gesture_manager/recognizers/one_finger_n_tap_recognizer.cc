// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace a11y {

struct OneFingerNTapRecognizer::Contest {
  Contest(std::unique_ptr<ContestMember> contest_member)
      : member(std::move(contest_member)), reject_task(member.get()) {}

  std::unique_ptr<ContestMember> member;
  // Indicates that a down event for the current tap is detected.
  bool tap_in_progress = false;
  // Keeps the count of the number of taps detected so far, for the gesture.
  int number_of_taps_detected = 0;
  // Async task used to schedule long-press timeout.
  async::TaskClosureMethod<ContestMember, &ContestMember::Reject> reject_task;
};

OneFingerNTapRecognizer::OneFingerNTapRecognizer(OnFingerTapGesture callback, int number_of_taps,
                                                 zx::duration tap_timeout,
                                                 zx::duration timeout_between_taps)
    : on_finger_tap_callback_(std::move(callback)),
      number_of_taps_in_gesture_(number_of_taps),
      tap_timeout_(tap_timeout),
      timeout_between_taps_(timeout_between_taps) {}

OneFingerNTapRecognizer::~OneFingerNTapRecognizer() = default;

void OneFingerNTapRecognizer::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_DCHECK(contest_);
  FX_DCHECK(pointer_event.has_phase())
      << DebugName() << ": Pointer event is missing phase information.";
  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      // If a tap is already detected, make sure the pointer_id and device_id of the new event,
      // matches with the previous one.
      if (contest_->number_of_taps_detected) {
        if (!ValidatePointerEvent(gesture_start_info_, pointer_event)) {
          FX_LOGS(INFO) << DebugName()
                        << ": Pointer Event is not a valid pointer event. Dropping current event.";
          contest_.reset();
          break;
        }
      }

      // Check if pointer event has all the required fields and initialize gesture_start_info and
      // gesture_context.
      if (!InitGestureInfo(pointer_event, &gesture_start_info_, &gesture_context_)) {
        FX_LOGS(INFO) << DebugName()
                      << ": Pointer Event is missing required fields. Dropping current event.";
        contest_.reset();
        break;
      }

      // If the gesture is already in progress then abandon this gesture since DownEvent()
      // represents the start of the gesture. Also, validate pointer event is valid for one finger
      // tap.
      if (contest_->tap_in_progress ||
          !PointerEventIsValidTap(gesture_start_info_, pointer_event)) {
        FX_LOGS(INFO) << DebugName()
                      << ": Pointer Event is not valid for current gesture."
                         "Dropping current event.";
        contest_.reset();
        break;
      }

      // Cancel task which would be scheduled for timeout between taps.
      contest_->reject_task.Cancel();

      // Schedule a task with kTapTimeout for the current tap to complete.
      contest_->reject_task.PostDelayed(async_get_default_dispatcher(), tap_timeout_);
      contest_->tap_in_progress = true;
      break;

    case fuchsia::ui::input::PointerEventPhase::MOVE:
      FX_DCHECK(contest_->tap_in_progress)
          << DebugName() << ": Pointer MOVE event received without preceding DOWN event.";

      // Validate the pointer_event for the gesture being performed.
      if (!ValidateEvent(pointer_event)) {
        contest_.reset();
      }
      break;

    case fuchsia::ui::input::PointerEventPhase::UP:
      FX_DCHECK(contest_->tap_in_progress)
          << DebugName() << ": Pointer Up event received without preceding DOWN event.";

      // Validate pointer_event for the gesture being performed.
      if (!ValidateEvent(pointer_event)) {
        contest_.reset();
        break;
      }

      // Tap is detected.
      contest_->number_of_taps_detected += 1;

      // Check if this is not the last tap of the gesture.
      if (contest_->number_of_taps_detected < number_of_taps_in_gesture_) {
        contest_->tap_in_progress = false;
        // Cancel task which was scheduled for detecting single tap.
        contest_->reject_task.Cancel();

        // Schedule task with delay of timeout_between_taps_.
        contest_->reject_task.PostDelayed(async_get_default_dispatcher(), timeout_between_taps_);
      } else {
        // Tap gesture is detected.
        contest_->member->Accept();
        contest_.reset();
      }
      break;
    default:
      break;
  }
}

void OneFingerNTapRecognizer::OnWin() { on_finger_tap_callback_(gesture_context_); }

void OneFingerNTapRecognizer::OnDefeat() { contest_.reset(); }

bool OneFingerNTapRecognizer::ValidateEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const {
  // Validate pointer event for one finger tap.
  return ValidatePointerEvent(gesture_start_info_, pointer_event) &&
         PointerEventIsValidTap(gesture_start_info_, pointer_event);
}

void OneFingerNTapRecognizer::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  ResetGestureInfo(&gesture_start_info_);
  ResetGestureContext(&gesture_context_);
  contest_ = std::make_unique<Contest>(std::move(contest_member));
}

std::string OneFingerNTapRecognizer::DebugName() const {
  return fxl::StringPrintf("OneFingerNTapRecognizer(n=%d)", number_of_taps_in_gesture_);
}

}  // namespace a11y
