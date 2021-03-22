// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/swipe_recognizer_base.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace a11y {

struct SwipeRecognizerBase::Contest {
  explicit Contest(std::unique_ptr<ContestMember> contest_member)
      : member(std::move(contest_member)), hold_timeout(member.get()) {}

  std::unique_ptr<ContestMember> member;
  // Indicates that a down event has been detected.

  // Async task used to schedule hold timeout.
  async::TaskClosureMethod<ContestMember, &ContestMember::Reject> hold_timeout;
};

SwipeRecognizerBase::SwipeRecognizerBase(SwipeGestureCallback callback, uint32_t number_of_fingers,
                                         zx::duration swipe_gesture_timeout,
                                         const std::string& debug_name)
    : swipe_gesture_callback_(std::move(callback)),
      swipe_gesture_timeout_(swipe_gesture_timeout),
      number_of_fingers_(number_of_fingers),
      debug_name_(debug_name) {}

SwipeRecognizerBase::~SwipeRecognizerBase() = default;

void SwipeRecognizerBase::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_DCHECK(contest_);

  if (!pointer_event.has_phase()) {
    return;
  }

  const auto& pointer_id = pointer_event.pointer_id();
  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::DOWN: {
      if (!InitializeStartingGestureContext(pointer_event, &gesture_context_)) {
        contest_->member->Reject();
        break;
      }

      if (!ValidatePointerEvent(gesture_context_, pointer_event)) {
        contest_->member->Reject();
        break;
      }

      if (NumberOfFingersOnScreen(gesture_context_) > number_of_fingers_) {
        contest_->member->Reject();
      } else if (NumberOfFingersOnScreen(gesture_context_) == 1) {
        // Schedule a task to declare defeat with a timeout equal to swipe_gesture_timeout_.
        contest_->hold_timeout.PostDelayed(async_get_default_dispatcher(), swipe_gesture_timeout_);
      }

      break;
    }
    case fuchsia::ui::input::PointerEventPhase::MOVE: {
      // Check that gesture info for the pointer_id exists and the pointer event is valid.
      if ((!ValidatePointerEvent(gesture_context_, pointer_event))) {
        contest_->member->Reject();
        break;
      }

      UpdateGestureContext(pointer_event, true /* finger is on screen */, &gesture_context_);

      // Check that fingers are moving in the direction of swipe recognizer only when all the
      // fingers are detected, there is no up event seen so far and length of swipe so far is longer
      // than kMinSwipeDistance.
      if ((NumberOfFingersOnScreen(gesture_context_) == number_of_fingers_)) {
        if (MinSwipeLengthAchieved(pointer_id, pointer_event) &&
            !ValidateSwipePath(pointer_id, pointer_event)) {
          contest_->member->Reject();
          break;
        }
      }

      break;
    }
    case fuchsia::ui::input::PointerEventPhase::UP: {
      // If we receive an UP event before number_of_fingers pointers have been
      // added to the screen, then reject.
      if (gesture_context_.starting_pointer_locations.size() != number_of_fingers_) {
        contest_->member->Reject();
        break;
      }

      // Validate pointer events.
      if (!(ValidatePointerEvent(gesture_context_, pointer_event) &&
            ValidateSwipePath(pointer_id, pointer_event))) {
        contest_->member->Reject();
        break;
      }

      UpdateGestureContext(pointer_event, false /* finger is off screen */, &gesture_context_);

      // If all the Up events are detected then call Accept.
      if (!NumberOfFingersOnScreen(gesture_context_)) {
        if (SquareDistanceBetweenPoints(gesture_context_.CurrentCentroid(false),
                                        gesture_context_.StartingCentroid(false)) >=
            kMinSwipeDistance * kMinSwipeDistance) {
          contest_->member->Accept();
          contest_.reset();
        } else {
          contest_->member->Reject();
          break;
        }
      }

      break;
    }
    default:
      break;
  }
}

void SwipeRecognizerBase::OnWin() { swipe_gesture_callback_(gesture_context_); }

void SwipeRecognizerBase::OnDefeat() { contest_.reset(); }

void SwipeRecognizerBase::ResetRecognizer() {
  contest_.reset();
  ResetGestureContext(&gesture_context_);
}

bool SwipeRecognizerBase::ValidateSwipePath(
    uint32_t pointer_id,
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const {
  // Verify that slope of line containing gesture start point and current pointer event location
  // falls within a pre-specified range.
  auto it = gesture_context_.starting_pointer_locations.find(pointer_id);
  if (it == gesture_context_.starting_pointer_locations.end()) {
    return false;
  }
  auto dx = pointer_event.ndc_point().x - it->second.ndc_point.x;
  auto dy = pointer_event.ndc_point().y - it->second.ndc_point.y;

  return SwipeHasValidSlopeAndDirection(dx, dy);
}

bool SwipeRecognizerBase::MinSwipeLengthAchieved(
    uint32_t pointer_id,
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const {
  auto it = gesture_context_.starting_pointer_locations.find(pointer_id);
  if (it == gesture_context_.starting_pointer_locations.end()) {
    return false;
  }

  float dx = pointer_event.ndc_point().x - it->second.ndc_point.x;
  float dy = pointer_event.ndc_point().y - it->second.ndc_point.y;

  float d2 = dx * dx + dy * dy;
  return d2 >= kMinSwipeDistance * kMinSwipeDistance;
}

void SwipeRecognizerBase::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  ResetGestureContext(&gesture_context_);
  contest_ = std::make_unique<Contest>(std::move(contest_member));
}

}  // namespace a11y
