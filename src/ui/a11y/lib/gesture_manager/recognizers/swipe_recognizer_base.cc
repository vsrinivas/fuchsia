// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/swipe_recognizer_base.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

namespace a11y {

struct SwipeRecognizerBase::Contest {
  Contest(std::unique_ptr<ContestMember> contest_member)
      : member(std::move(contest_member)), hold_timeout(member.get()) {}

  std::unique_ptr<ContestMember> member;
  // Indicates that a down event has been detected.
  bool in_progress = false;
  // Async task used to schedule hold timeout.
  async::TaskClosureMethod<ContestMember, &ContestMember::Reject> hold_timeout;
};

SwipeRecognizerBase::SwipeRecognizerBase(SwipeGestureCallback callback,
                                         zx::duration swipe_gesture_timeout)
    : swipe_gesture_callback_(std::move(callback)), swipe_gesture_timeout_(swipe_gesture_timeout) {}

SwipeRecognizerBase::~SwipeRecognizerBase() = default;

void SwipeRecognizerBase::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_DCHECK(contest_);

  if (!pointer_event.has_phase()) {
    FX_LOGS(ERROR) << "Pointer event is missing phase information.";
    return;
  }

  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      if (!InitGestureInfo(pointer_event, &gesture_start_info_, &gesture_context_)) {
        FX_LOGS(ERROR) << "Pointer Event is missing required fields. Dropping current event.";
        contest_->member->Reject();
      } else if (contest_->in_progress ||
                 !ValidatePointerEvent(gesture_start_info_, pointer_event)) {
        contest_->member->Reject();
      } else {
        // Schedule a task to declare defeat with a timeout equal to swipe_gesture_timeout_.
        contest_->hold_timeout.PostDelayed(async_get_default_dispatcher(), swipe_gesture_timeout_);
        contest_->in_progress = true;
      }
      break;
    case fuchsia::ui::input::PointerEventPhase::MOVE:
      FX_DCHECK(contest_->in_progress)
          << "Pointer MOVE event received without preceding DOWN event.";

      if (!(ValidatePointerEvent(gesture_start_info_, pointer_event) &&
            ValidateSwipePath(pointer_event))) {
        contest_->member->Reject();
      }
      break;
    case fuchsia::ui::input::PointerEventPhase::UP:
      FX_DCHECK(contest_->in_progress) << "Pointer UP event received without preceding DOWN event.";

      if (!(ValidatePointerEvent(gesture_start_info_, pointer_event) &&
            ValidateSwipePath(pointer_event) && ValidateSwipeDistance(pointer_event))) {
        contest_->member->Reject();
      } else {
        contest_->member->Accept();
        contest_.reset();
      }
      break;
    default:
      break;
  }
}

void SwipeRecognizerBase::OnWin() { swipe_gesture_callback_(gesture_context_); }

void SwipeRecognizerBase::OnDefeat() { contest_.reset(); }

bool SwipeRecognizerBase::ValidateSwipePath(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // Verify that slope of line containing gesture start point and current pointer event location
  // falls within pre-specified range.
  auto dx = pointer_event.ndc_point().x - gesture_start_info_.starting_ndc_position.x;
  auto dy = pointer_event.ndc_point().y - gesture_start_info_.starting_ndc_position.y;

  return ValidateSwipeSlopeAndDirection(dx, dy);
}

bool SwipeRecognizerBase::ValidateSwipeDistance(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // Check if the distance between the pointer event and the start point is within the allowable
  // range.
  auto dx = pointer_event.ndc_point().x - gesture_start_info_.starting_ndc_position.x;
  auto dy = pointer_event.ndc_point().y - gesture_start_info_.starting_ndc_position.y;

  auto d2 = dx * dx + dy * dy;

  return d2 >= kMinSwipeDistance * kMinSwipeDistance && d2 <= kMaxSwipeDistance * kMaxSwipeDistance;
}

void SwipeRecognizerBase::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  ResetGestureInfo(&gesture_start_info_);
  ResetGestureContext(&gesture_context_);
  contest_ = std::make_unique<Contest>(std::move(contest_member));
}

}  // namespace a11y
