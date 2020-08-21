// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/two_finger_n_tap_recognizer.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include <set>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace a11y {

struct TwoFingerNTapRecognizer::Contest {
  Contest(std::unique_ptr<ContestMember> contest_member)
      : member(std::move(contest_member)), reject_task(member.get()) {}

  std::unique_ptr<ContestMember> member;
  // Indicates whether two fingers have been on the screen at the same time
  // during the current tap.
  bool tap_in_progress = false;
  // Indicates how many fingers a down event has been detected for.
  std::set<uint32_t> fingers_on_screen = {};
  // Keeps the count of the number of taps detected so far, for the gesture.
  int number_of_taps_detected = 0;
  // Async task used to schedule long-press timeout.
  async::TaskClosureMethod<ContestMember, &ContestMember::Reject> reject_task;
};

TwoFingerNTapRecognizer::TwoFingerNTapRecognizer(OnTwoFingerTapGesture callback, int number_of_taps)
    : on_two_finger_tap_callback_(std::move(callback)),
      number_of_taps_in_gesture_(number_of_taps) {}

TwoFingerNTapRecognizer::~TwoFingerNTapRecognizer() = default;

void TwoFingerNTapRecognizer::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_DCHECK(contest_);
  FX_DCHECK(pointer_event.has_pointer_id()) << "Pointer event is missing pointer id.";
  const auto pointer_id = pointer_event.pointer_id();
  FX_DCHECK(pointer_event.has_phase()) << "Pointer event is missing phase information.";
  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      // If we receive a DOWN event when there are already two fingers on the
      // screen, then either we've received a second DOWN event for one of the fingers that's
      // already on the screen, or we've received a DOWN event for a third
      // finger. In either case, we should abandon the current gesture.
      if (contest_->fingers_on_screen.size() >= 2) {
        ResetGesture("More than two fingers present on the screen. Dropping current event.");
        break;
      }

      // If we receive a DOWN event when there is a tap in progress, then we
      // should abandon the gesture.
      // NOTE: this is a distinct check from the one above, and is required to
      // ensure that the number of fingers touching the screen decreases
      // monotonically once the first finger is removed.
      // For example,
      // consider the case of finger 1 DOWN, finger 2 DOWN, finger 2 UP, finger
      // 2 DOWN. Clearly, this is not a two-finger tap, but at the time of the
      // second "finger 2 DOWN" event, contest->fingers_on_screen.size() would
      // be 1, so the check above would pass.
      if (contest_->tap_in_progress) {
        ResetGesture("DOWN event received while tap is in progress. Dropping current event.");
        break;
      }

      //  If we receive successive DOWN events for the same pointer without an
      //  UP event, then we should abandon the current gesture.
      if (contest_->fingers_on_screen.find(pointer_id) != contest_->fingers_on_screen.end()) {
        ResetGesture(
            "Consecutive DOWN events received for the same finger. Dropping current event.");
        break;
      }

      if (contest_->number_of_taps_detected) {
        // If this is not the first tap, then make sure the pointer_id and device_id of the
        // new_event matches with the previous event.
        if (!StartInfoExist(pointer_event) ||
            !ValidatePointerEvent(start_info_by_finger_.at(pointer_id), pointer_event)) {
          FX_LOGS(INFO) << "Pointer Event is not a valid pointer event. Dropping current event.";
          contest_.reset();
          break;
        }
      }

      // Check if pointer event has all the required fields and initialize gesture_start_info and
      // gesture_context.
      // NOTE: We will update gesture_context_ for both fingers, so it will reflect the location of
      // the second finger to touch the screen during the first tap of the gesture.
      if (!InitGestureInfo(pointer_event, &start_info_by_finger_[pointer_id], &gesture_context_)) {
        ResetGesture("Pointer Event is missing required fields. Dropping current event.");
        break;
      }

      // Check that the pointer event is valid for the current gesture.
      if (!StartInfoExist(pointer_event) ||
          !PointerEventIsValidTap(start_info_by_finger_.at(pointer_id), pointer_event)) {
        ResetGesture("Pointer Event is not a valid pointer event. Dropping current event.");
        break;
      }

      // Cancel task which would be scheduled for timeout between taps.
      contest_->reject_task.Cancel();

      // Schedule a task with kTapTimeout for the current tap to complete.
      contest_->fingers_on_screen.insert(pointer_id);
      contest_->tap_in_progress = (contest_->fingers_on_screen.size() == 2);
      // Only start the timeout once two fingers are on the screen together.
      if (contest_->tap_in_progress) {
        contest_->reject_task.PostDelayed(async_get_default_dispatcher(), kTapTimeout);
      }
      break;

    case fuchsia::ui::input::PointerEventPhase::MOVE:
      FX_DCHECK(contest_->fingers_on_screen.find(pointer_id) != contest_->fingers_on_screen.end())
          << "Pointer MOVE event received without preceding DOWN event.";

      // Validate the pointer_event for the gesture being performed.
      if (!EventIsValid(pointer_event)) {
        ResetGesture("Pointer event is not valid for current gesture. Dropping current event.");
      }
      break;

    case fuchsia::ui::input::PointerEventPhase::UP:
      FX_DCHECK(contest_->fingers_on_screen.find(pointer_id) != contest_->fingers_on_screen.end())
          << "Pointer UP event received without preceding DOWN event.";

      // Validate pointer_event for the gesture being performed.
      if (!EventIsValid(pointer_event)) {
        ResetGesture("Pointer Event is not valid for current gesture. Dropping current event.");
        break;
      }

      contest_->fingers_on_screen.erase(pointer_id);

      // The number of fingers on screen during a multi-finger tap should
      // monotonically increase from 0 to 2, and
      // then monotonically decrease back to 0. If a finger is removed before
      // number_of_fingers_in_gesture_ fingers are on the screen simultaneously,
      // then we should reject this gesture.
      if (!contest_->tap_in_progress) {
        ResetGesture(
            "Insufficient fingers on screen before first finger was lifted. Dropping current "
            "event.");
        break;
      }

      // If there are still fingers on the screen, then we haven't yet detected
      // a full tap, so there's no more work to do at this point.
      if (!contest_->fingers_on_screen.empty()) {
        break;
      }

      // If we've made it this far, we know that (1) Two fingers were on screen
      // simultaneously during the current gesture, and (2) The two fingers have
      // now been removed, without any interceding finger DOWN events.
      // Therefore, we can conclude that a complete two-finger tap has occurred.
      contest_->number_of_taps_detected++;

      // Check if this is not the last tap of the gesture.
      if (contest_->number_of_taps_detected < number_of_taps_in_gesture_) {
        contest_->tap_in_progress = false;
        contest_->fingers_on_screen.clear();
        // Cancel task which was scheduled for detecting single tap.
        contest_->reject_task.Cancel();

        // Schedule task with delay of timeout_between_taps_.
        contest_->reject_task.PostDelayed(async_get_default_dispatcher(), kTimeoutBetweenTaps);
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

void TwoFingerNTapRecognizer::ResetGesture(const std::string& reason) {
  FX_LOGS(INFO) << reason;
  start_info_by_finger_.clear();
  contest_.reset();
}

void TwoFingerNTapRecognizer::OnWin() { on_two_finger_tap_callback_(gesture_context_); }

void TwoFingerNTapRecognizer::OnDefeat() { contest_.reset(); }

bool TwoFingerNTapRecognizer::EventIsValid(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const {
  if (!StartInfoExist(pointer_event)) {
    return false;
  }

  const auto& gesture_start_info_for_finger = start_info_by_finger_.at(pointer_event.pointer_id());
  // Validate pointer event for one finger tap.
  return ValidatePointerEvent(gesture_start_info_for_finger, pointer_event) &&
         PointerEventIsValidTap(gesture_start_info_for_finger, pointer_event);
}
bool TwoFingerNTapRecognizer::StartInfoExist(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const {
  FX_DCHECK(pointer_event.has_pointer_id()) << "Pointer event missing pointer id.";
  const auto pointer_id = pointer_event.pointer_id();
  return start_info_by_finger_.find(pointer_id) != start_info_by_finger_.end();
}

void TwoFingerNTapRecognizer::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  start_info_by_finger_.clear();
  ResetGestureContext(&gesture_context_);
  contest_ = std::make_unique<Contest>(std::move(contest_member));
}

std::string TwoFingerNTapRecognizer::DebugName() const {
  return fxl::StringPrintf("TwoFingerNTapRecognizer(n=%d)", number_of_taps_in_gesture_);
}

}  // namespace a11y
