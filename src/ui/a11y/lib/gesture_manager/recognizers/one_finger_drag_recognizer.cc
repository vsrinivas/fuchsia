// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_drag_recognizer.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

namespace a11y {

struct OneFingerDragRecognizer::Contest {
  Contest(std::unique_ptr<ContestMember> contest_member)
      : member(std::move(contest_member)), claim_win_task(member.get()) {}

  std::unique_ptr<ContestMember> member;
  unsigned int pointer_count = 0;
  bool won = false;
  // Async task that claims a win if the drag gesture lasts longer than a delay.
  async::TaskClosureMethod<ContestMember, &ContestMember::Accept> claim_win_task;
};

OneFingerDragRecognizer::OneFingerDragRecognizer(DragGestureCallback on_drag_started,
                                                 DragGestureCallback on_drag_update,
                                                 DragGestureCallback on_drag_complete,
                                                 zx::duration drag_gesture_delay)
    : on_drag_started_(std::move(on_drag_started)),
      on_drag_update_(std::move(on_drag_update)),
      on_drag_complete_(std::move(on_drag_complete)),
      drag_gesture_delay_(drag_gesture_delay) {}

OneFingerDragRecognizer::~OneFingerDragRecognizer() = default;

void OneFingerDragRecognizer::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_DCHECK(contest_);
  FX_DCHECK(pointer_event.has_phase());

  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      ++contest_->pointer_count;

      if (contest_->pointer_count > 1) {
        // Ignore pointers past the first.
        return;
      }

      if (InitGestureInfo(pointer_event, &previous_update_location_info_, &gesture_context_) &&
          ValidatePointerEvent(previous_update_location_info_, pointer_event)) {
        // Schedule a task to attempt to claim win after duration of drag_gesture_delay_.
        contest_->claim_win_task.PostDelayed(async_get_default_dispatcher(), drag_gesture_delay_);
      } else {
        contest_.reset();
      }

      break;

    case fuchsia::ui::input::PointerEventPhase::MOVE:
      FX_DCHECK(contest_->pointer_count)
          << DebugName() << ": Pointer MOVE event received without preceding DOWN event.";
      if (contest_->pointer_count > 1) {
        // Ignore pointers past the first.
        return;
      }

      // TODO: Move ValidatePointerEvent check into InitGestureInfo method.
      if (!ValidatePointerEvent(previous_update_location_info_, pointer_event)) {
        contest_.reset();
        return;
      }

      // IF this recognizer is the contest winner, previous_update_location_info_ reflects the
      // location of the previous update. Otherwise, previous_update_location_info_ reflects the
      // location of the previous pointer event ingested.
      //
      // THEREFORE, IF the recognizer is NOT yet the contest winner, we should update the previous
      // location info, but should NOT call the update callback. IF the recognizer is the contest
      // winner, we only want to update the previous location and invoke the update callback if the
      // distance between the location of the current event and the previous update exceeds the
      // minimum threshold.
      if (!contest_->won) {
        InitGestureInfo(pointer_event, &previous_update_location_info_, &gesture_context_);
      } else if (DragDistanceExceedsUpdateThreshold(pointer_event)) {
        InitGestureInfo(pointer_event, &previous_update_location_info_, &gesture_context_);
        on_drag_update_(gesture_context_);
      }

      break;

    case fuchsia::ui::input::PointerEventPhase::UP:
      FX_DCHECK(contest_->pointer_count);
      --contest_->pointer_count;

      if (contest_->pointer_count) {
        // Ignore (removal of) pointers past the first.
        return;
      }

      if (ValidatePointerEvent(previous_update_location_info_, pointer_event)) {
        // Update gesture context to reflect UP event info.
        InitGestureInfo(pointer_event, &previous_update_location_info_, &gesture_context_);

        if (contest_->won) {
          on_drag_complete_(gesture_context_);
        }
      }

      contest_.reset();

      break;

    default:
      break;
  }
}

void OneFingerDragRecognizer::OnWin() {
  if (contest_) {
    contest_->won = true;
    // The gesture has been recognized and we inform about its start.
    on_drag_started_(gesture_context_);
    // We need to call on_drag_update_ immediately after successfully claiming a win, because it's
    // possible that no update will ever occur if no further MOVE events are ingested, OR if the
    // locations of these events are close to the location of the last event ingested before the win
    // was claimed.
    on_drag_update_(gesture_context_);
  } else {
    // It's possible that we don't get awarded the win until after the gesture has completed, in
    // which case just call the start and complete handler.
    on_drag_started_(gesture_context_);
    on_drag_complete_(gesture_context_);
  }
}

void OneFingerDragRecognizer::OnDefeat() { contest_.reset(); }

bool OneFingerDragRecognizer::DragDistanceExceedsUpdateThreshold(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // Check if distance between previous update point and current event exceeds specified minimum
  // threshold.
  auto dx = pointer_event.ndc_point().x - previous_update_location_info_.starting_ndc_position.x;
  auto dy = pointer_event.ndc_point().y - previous_update_location_info_.starting_ndc_position.y;

  return dx * dx + dy * dy >= kMinDragDistanceForUpdate * kMinDragDistanceForUpdate;
}

void OneFingerDragRecognizer::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  ResetGestureInfo(&previous_update_location_info_);
  ResetGestureContext(&gesture_context_);
  contest_ = std::make_unique<Contest>(std::move(contest_member));
}

std::string OneFingerDragRecognizer::DebugName() const { return "one_finger_drag_recognizer"; }

}  // namespace a11y
