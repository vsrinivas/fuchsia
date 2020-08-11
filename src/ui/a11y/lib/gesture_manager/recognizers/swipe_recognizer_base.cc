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
  bool in_progress = false;
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
    FX_LOGS(INFO) << debug_name_ << ": Pointer event is missing phase information.";
    return;
  }

  GestureInfo gesture_info;
  const auto& pointer_id = pointer_event.pointer_id();
  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      // Check that Up event is not detected before any Down event.
      if (number_of_up_event_detected_) {
        FX_LOGS(INFO) << debug_name_
                      << ": Down Event detected after 'Up' event. Dropping current event.";
        contest_->member->Reject();
        break;
      }

      if (!InitGestureInfo(pointer_event, &gesture_info, &gesture_context_)) {
        FX_LOGS(INFO) << debug_name_
                      << ": Pointer Event is missing required fields. Dropping current event.";
        contest_->member->Reject();
        break;
      }

      // For the first down event, make sure the contest is not in progress. For subsequent down
      // events, contest should be in progress.
      if (gesture_info_map_.empty() == contest_->in_progress) {
        FX_LOGS(INFO) << debug_name_ << ": Failed because contest was already in progress.";
        contest_->member->Reject();
        break;
      }

      if (!ValidatePointerEvent(gesture_info, pointer_event)) {
        FX_LOGS(INFO) << debug_name_ << ": Failed because of pointer event validation.";
        contest_->member->Reject();
        break;
      }

      gesture_info_map_[pointer_id] = std::move(gesture_info);

      if (gesture_info_map_.size() > number_of_fingers_) {
        FX_LOGS(INFO) << debug_name_
                      << ": More fingers detected than expected. Dropping current event.";
        contest_->member->Reject();
      } else if (gesture_info_map_.size() == 1) {
        // Schedule a task to declare defeat with a timeout equal to swipe_gesture_timeout_.
        contest_->hold_timeout.PostDelayed(async_get_default_dispatcher(), swipe_gesture_timeout_);
        contest_->in_progress = true;
      }
      UpdateLastPointerPosition(pointer_id, pointer_event);

      break;
    case fuchsia::ui::input::PointerEventPhase::MOVE: {
      FX_DCHECK(contest_->in_progress)
          << "Pointer MOVE event received without preceding DOWN event.";

      // Check that gesture info for the pointer_id exists and the pointer event is valid.
      auto it = gesture_info_map_.find(pointer_id);
      if ((it == gesture_info_map_.end()) || (!ValidatePointerEvent(it->second, pointer_event))) {
        contest_->member->Reject();
        break;
      }

      // Check that fingers are moving in the direction of swipe recognizer only when all the
      // fingers are detected, there is no up event seen so far and length of swipe so far is longer
      // than kMinSwipeDistance.
      if ((gesture_info_map_.size() == number_of_fingers_) && !number_of_up_event_detected_) {
        if (MinSwipeLengthAchieved(pointer_id, pointer_event) &&
            !ValidateSwipePath(pointer_id, pointer_event)) {
          contest_->member->Reject();
          break;
        }
      }

      UpdateLastPointerPosition(pointer_id, pointer_event);
      break;
    }
    case fuchsia::ui::input::PointerEventPhase::UP: {
      FX_DCHECK(contest_->in_progress) << "Pointer UP event received without preceding DOWN event.";
      number_of_up_event_detected_++;

      // Check if the all the Down events are detected.
      if (gesture_info_map_.size() != number_of_fingers_) {
        FX_LOGS(INFO) << debug_name_
                      << ": Failed because an up event is detected before all the down events.";
        contest_->member->Reject();
        break;
      }

      // Validate pointer events.
      auto it = gesture_info_map_.find(pointer_id);
      if (!(ValidatePointerEvent(it->second, pointer_event) &&
            ValidateSwipePath(pointer_id, pointer_event) &&
            ValidateSwipeDistance(pointer_id, pointer_event))) {
        FX_LOGS(INFO) << debug_name_ << ": Failed while validating pointer events.";
        contest_->member->Reject();
        break;
      }

      // If all the Up events are detected then call Accept.
      if (number_of_up_event_detected_ == number_of_fingers_) {
        contest_->member->Accept();
        contest_.reset();
      }

      break;
    }
    default:
      break;
  }
}

void SwipeRecognizerBase::OnWin() { swipe_gesture_callback_(gesture_context_); }

void SwipeRecognizerBase::OnDefeat() { contest_.reset(); }

bool SwipeRecognizerBase::ValidateSwipePath(
    uint32_t pointer_id,
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const {
  // Verify that slope of line containing gesture start point and current pointer event location
  // falls within a pre-specified range.
  auto it = gesture_info_map_.find(pointer_id);
  if (it == gesture_info_map_.end()) {
    return false;
  }
  auto dx = pointer_event.ndc_point().x - it->second.starting_ndc_position.x;
  auto dy = pointer_event.ndc_point().y - it->second.starting_ndc_position.y;

  return SwipeHasValidSlopeAndDirection(dx, dy);
}

bool SwipeRecognizerBase::ValidateSwipeDistance(
    uint32_t pointer_id,
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const {
  // Check if the distance between the pointer event and the start point is within the allowable
  // range.
  auto it = gesture_info_map_.find(pointer_id);
  if (it == gesture_info_map_.end()) {
    return false;
  }
  float dx = pointer_event.ndc_point().x - it->second.starting_ndc_position.x;
  float dy = pointer_event.ndc_point().y - it->second.starting_ndc_position.y;

  float d2 = dx * dx + dy * dy;

  return d2 >= kMinSwipeDistance * kMinSwipeDistance && d2 <= kMaxSwipeDistance * kMaxSwipeDistance;
}

bool SwipeRecognizerBase::MinSwipeLengthAchieved(
    uint32_t pointer_id,
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const {
  auto it = gesture_info_map_.find(pointer_id);
  if (it == gesture_info_map_.end()) {
    return false;
  }
  float dx = pointer_event.ndc_point().x - it->second.starting_ndc_position.x;
  float dy = pointer_event.ndc_point().y - it->second.starting_ndc_position.y;

  float d2 = dx * dx + dy * dy;
  return d2 >= kMinSwipeDistance * kMinSwipeDistance;
}

void SwipeRecognizerBase::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  ResetGestureContext(&gesture_context_);
  contest_ = std::make_unique<Contest>(std::move(contest_member));
  number_of_up_event_detected_ = 0;
  gesture_info_map_.clear();
  stopping_position_.clear();
}

void SwipeRecognizerBase::UpdateLastPointerPosition(
    uint32_t pointer_id, const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  GestureInfo gesture_info;
  GestureContext dummy_context;
  if (!InitGestureInfo(pointer_event, &gesture_info, &dummy_context)) {
    FX_LOGS(INFO) << debug_name_
                  << ": Pointer Event is missing required fields. Dropping current event.";
    contest_->member->Reject();
    return;
  }
  stopping_position_[pointer_id] = std::move(gesture_info);
}

}  // namespace a11y
