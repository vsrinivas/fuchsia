// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_drag_recognizer.h"

#include <valarray>

#include "lib/async/cpp/task.h"
#include "lib/async/default.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

namespace a11y {

OneFingerDragRecognizer::OneFingerDragRecognizer(DragGestureCallback on_drag_update,
                                                 DragGestureCallback on_drag_cancel,
                                                 DragGestureCallback on_drag_complete,
                                                 zx::duration drag_gesture_delay)
    : on_drag_update_(std::move(on_drag_update)),
      on_drag_cancel_(std::move(on_drag_cancel)),
      on_drag_complete_(std::move(on_drag_complete)),
      claim_win_task_(this),
      drag_gesture_delay_(drag_gesture_delay) {}

void OneFingerDragRecognizer::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FXL_DCHECK(pointer_event.has_phase());

  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      if (gesture_state_ != DragGestureState::kNotStarted) {
        AbandonGesture();
        break;
      }

      if (!InitGestureInfo(pointer_event, &previous_update_location_info_, &gesture_context_)) {
        AbandonGesture();
        break;
      }

      if (!ValidatePointerEvent(previous_update_location_info_, pointer_event)) {
        AbandonGesture();
        break;
      }

      // Schedule a task to attempt to claim win after duration of drag_gesture_delay_.
      claim_win_task_.PostDelayed(async_get_default_dispatcher(), drag_gesture_delay_);
      gesture_state_ = DragGestureState::kInProgress;

      break;

    case fuchsia::ui::input::PointerEventPhase::MOVE:
      FXL_DCHECK(gesture_state_ == DragGestureState::kInProgress
                 || gesture_state_ == DragGestureState::kIsWinner);

      // TODO: Move ValidatePointerEvent check into InitGestureInfo method.
      if (!ValidatePointerEvent(previous_update_location_info_, pointer_event)) {
        AbandonGesture();
        break;
      }

      // IF the drag delay has elapsed and this recognizer is the contest winner,
      // previous_update_location_info_ reflects the location of the previous update. IF a drag
      // has NOT been detected or the recognizer is NOT the winner, previous_update_location_info_
      // reflects the location of the previous pointer event ingested.
      // THEREFORE, IF the drag delay has NOT yet elapsed OR the recognizer is NOT yet the contest winner,
      // we should update the previous location info, but should NOT call the update callback.
      // IF the drag delay has elapsed AND the recognizer is the contest winner, we only want to update
      // the previous location and invoke the update callback if the distance between the location
      // of the current event and the previous update exceeds the minimum threshold.
      if (!drag_delay_elapsed_ || gesture_state_ != DragGestureState::kIsWinner) {
        InitGestureInfo(pointer_event, &previous_update_location_info_, &gesture_context_);
        break;
      }

      if (DragDistanceExceedsUpdateThreshold(pointer_event)) {
        InitGestureInfo(pointer_event, &previous_update_location_info_, &gesture_context_);
        on_drag_update_(gesture_context_);
      }

      break;

    case fuchsia::ui::input::PointerEventPhase::UP:
      if ((gesture_state_ != DragGestureState::kInProgress && gesture_state_ != DragGestureState::kIsWinner)
          || !ValidatePointerEvent(previous_update_location_info_, pointer_event)) {
        AbandonGesture();
        break;
      }

      // Cancel claim-win task.
      claim_win_task_.Cancel();

      // Update gesture context to reflect UP event info.
      InitGestureInfo(pointer_event, &previous_update_location_info_, &gesture_context_);

      // If a drag gesture did indeed occur, invoke drag completion callback.
      if (gesture_state_ == DragGestureState::kIsWinner) {
        on_drag_complete_(gesture_context_);
      }

      contest_member_.reset();
      gesture_state_ = DragGestureState::kDone;

      break;

    default:
      break;
  }
}

void OneFingerDragRecognizer::OnWin() { gesture_state_ = DragGestureState::kIsWinner; }

void OneFingerDragRecognizer::OnDefeat() {
  claim_win_task_.Cancel();
  contest_member_.reset();

  // The cancel callback should only be invoked if at least one update has been given.
  if (drag_delay_elapsed_ && gesture_state_ == DragGestureState::kIsWinner) {
   on_drag_cancel_(gesture_context_);
  }

  gesture_state_ = DragGestureState::kDone;
}

void OneFingerDragRecognizer::AbandonGesture() {
  FX_DCHECK(contest_member_) << "No active contest.";

  contest_member_->Reject();
}

void OneFingerDragRecognizer::OnDragDelayComplete() {
  FX_DCHECK(contest_member_);

  // Mark that the delay period has elapsed.
  drag_delay_elapsed_ = true;

  // We only need to call Accept() if the recognizer is not already the winner.
  // If the recognizer is not already the winner and the call to Accept() fails, then
  // we should abandon the gesture.
  if (gesture_state_ != DragGestureState::kIsWinner && !contest_member_->Accept()) {
    AbandonGesture();
    return;
  }

  // We need to call on_drag_update_ immediately after successfully claiming a win, because it's
  // possible that no update will ever occur if no further MOVE events are ingested, OR if the
  // locations of these events are close to the location of the last event ingested before the win
  // was claimed.
  on_drag_update_(gesture_context_);
}

void OneFingerDragRecognizer::ResetState() {
  claim_win_task_.Cancel();

  ResetGestureInfo(&previous_update_location_info_);

  ResetGestureContext(&gesture_context_);

  gesture_state_ = DragGestureState::kNotStarted;
}

bool OneFingerDragRecognizer::DragDistanceExceedsUpdateThreshold(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // Check if distance between previous update point and current event exceeds specified minimum
  // threshold.
  auto x_displacement =
      pointer_event.ndc_point().x - previous_update_location_info_.starting_ndc_position.x;
  auto y_displacement =
      pointer_event.ndc_point().y - previous_update_location_info_.starting_ndc_position.y;

  auto distance_from_last_update_point = std::hypot(x_displacement, y_displacement);

  return (distance_from_last_update_point > kMinDragDistanceForUpdate);
}

void OneFingerDragRecognizer::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  ResetState();
  contest_member_ = std::move(contest_member);
}

}  // namespace a11y
