// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/two_finger_drag_recognizer.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

namespace a11y {

struct TwoFingerDragRecognizer::Contest {
  Contest(std::unique_ptr<ContestMember> contest_member)
      : member(std::move(contest_member)),
        claim_win_task(member.get()),
        reject_task(member.get()) {}

  std::unique_ptr<ContestMember> member;
  bool won = false;
  // Indicates whether two fingers have had DOWN events.
  bool both_fingers_down = false;
  // Async task that claims a win if the drag gesture lasts longer than a delay.
  async::TaskClosureMethod<ContestMember, &ContestMember::Accept> claim_win_task;
  // Async task that claims a win if the drag gesture lasts longer than a delay.
  async::TaskClosureMethod<ContestMember, &ContestMember::Reject> reject_task;
};

TwoFingerDragRecognizer::TwoFingerDragRecognizer(DragGestureCallback on_drag_started,
                                                 DragGestureCallback on_drag_update,
                                                 DragGestureCallback on_drag_complete,
                                                 zx::duration drag_gesture_delay)
    : on_drag_started_(std::move(on_drag_started)),
      on_drag_update_(std::move(on_drag_update)),
      on_drag_complete_(std::move(on_drag_complete)),
      drag_gesture_delay_(drag_gesture_delay) {}

TwoFingerDragRecognizer::~TwoFingerDragRecognizer() = default;

void TwoFingerDragRecognizer::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_DCHECK(contest_);
  FX_DCHECK(pointer_event.has_phase());

  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      // If there are already two or more fingers on screen, then we should not
      // receive any further DOWN events.
      if (NumberOfFingersOnScreen(gesture_context_) >= 2) {
        ResetRecognizer();
        return;
      }

      if (!InitializeStartingGestureContext(pointer_event, &gesture_context_) ||
          !ValidatePointerEvent(gesture_context_, pointer_event)) {
        ResetRecognizer();
        return;
      }

      FX_DCHECK(NumberOfFingersOnScreen(gesture_context_) <= 2);

      contest_->both_fingers_down = NumberOfFingersOnScreen(gesture_context_) == 2;

      // If this DOWN event is for the second finger, both fingers are now on
      // screen, so set a task to accept the gesture after the drag delay has
      // elapsed.
      //
      // We expect both fingers to come down on screen within a small window of time.
      // NOTE: WIthout this requirement, it would be impossible to discern
      // between a one-finger-drag and the beginning of a two-finger-drag during
      // which the second finger hasn't come down yet.
      // If this DOWN event is for the first finger on screen, set a task to
      // reject if the second finger does not come down in a timely manner.
      if (contest_->both_fingers_down) {
        contest_->reject_task.Cancel();
        contest_->claim_win_task.PostDelayed(async_get_default_dispatcher(), drag_gesture_delay_);
      } else {
        contest_->reject_task.PostDelayed(async_get_default_dispatcher(),
                                          kMaxSecondFingerDownDelay);
      }

      break;

    case fuchsia::ui::input::PointerEventPhase::MOVE:
      // If there are more than two fingers on screen, then we should reset.
      if (NumberOfFingersOnScreen(gesture_context_) > 2) {
        ResetRecognizer();
        return;
      }

      if (!ValidatePointerEvent(gesture_context_, pointer_event)) {
        ResetRecognizer();
        return;
      }

      // Update pointer book-keeping.
      UpdateGestureContext(pointer_event, true /*finger is on screen*/, &gesture_context_);

      // Only send gesture updates if the gesture has been accepted.
      // Otherwise, check if two fingers are on screen AND either:
      //   (1) The distance between the two fingers has changed by a factor of
      //   kFingerSeparationThresholdFactor.
      //   (2) The midpoint of the two fingers has moved by some threshold
      //   kDragDisplacementThreshold.
      if (contest_->won) {
        on_drag_update_(gesture_context_);
      } else if (contest_->both_fingers_down &&
                 (DisplacementExceedsThreshold() || SeparationExceedsThreshold())) {
        contest_->member->Accept();
      }

      break;

    case fuchsia::ui::input::PointerEventPhase::UP:
      if (!ValidatePointerEvent(gesture_context_, pointer_event)) {
        ResetRecognizer();
        return;
      }

      // If two fingers were never on screen, we should reject.
      if (!contest_->both_fingers_down) {
        ResetRecognizer();
        break;
      }

      // Update gesture context to reflect UP event info.
      UpdateGestureContext(pointer_event, false /*finger is off screen*/, &gesture_context_);

      // Consider the drag complete after the first finger has been lifted.
      if (contest_->won) {
        on_drag_complete_(gesture_context_);
      }

      ResetRecognizer();

      break;

    default:
      break;
  }
}

void TwoFingerDragRecognizer::OnWin() {
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
    ResetRecognizer();
  }
}

void TwoFingerDragRecognizer::OnDefeat() { ResetRecognizer(); }

void TwoFingerDragRecognizer::ResetRecognizer() {
  ResetGestureContext(&gesture_context_);
  contest_.reset();
}

bool TwoFingerDragRecognizer::DisplacementExceedsThreshold() {
  return SquareDistanceBetweenPoints(
             gesture_context_.StartingCentroid(false /*use_local_coordinates*/),
             gesture_context_.CurrentCentroid(false /*use_local_coordinates*/)) >=
         kDragDisplacementThreshold * kDragDisplacementThreshold;
}

bool TwoFingerDragRecognizer::SeparationExceedsThreshold() {
  if (gesture_context_.starting_pointer_locations.size() != 2 ||
      gesture_context_.current_pointer_locations.size() != 2) {
    return false;
  }

  auto starting_locations_it = gesture_context_.starting_pointer_locations.begin();
  auto pointer_1_start = starting_locations_it->second;
  auto pointer_2_start = (++starting_locations_it)->second;
  auto starting_squared_distance =
      SquareDistanceBetweenPoints(pointer_1_start.ndc_point, pointer_2_start.ndc_point);

  auto current_locations_it = gesture_context_.current_pointer_locations.begin();
  auto pointer_1_current = current_locations_it->second;
  auto pointer_2_current = (++current_locations_it)->second;
  auto current_squared_distance =
      SquareDistanceBetweenPoints(pointer_1_current.ndc_point, pointer_2_current.ndc_point);

  auto larger_distance = std::max(starting_squared_distance, current_squared_distance);
  auto smaller_distance = std::min(starting_squared_distance, current_squared_distance);

  return larger_distance >=
         smaller_distance * kFingerSeparationThresholdFactor * kFingerSeparationThresholdFactor;
}

void TwoFingerDragRecognizer::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  ResetRecognizer();
  contest_ = std::make_unique<Contest>(std::move(contest_member));
}

std::string TwoFingerDragRecognizer::DebugName() const { return "two_finger_drag_recognizer"; }

}  // namespace a11y
