// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/m_finger_n_tap_drag_recognizer.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include <set>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace a11y {

struct MFingerNTapDragRecognizer::Contest {
  Contest(std::unique_ptr<ContestMember> contest_member)
      : member(std::move(contest_member)),
        tap_length_timeout(member.get()),
        tap_interval_timeout(member.get()),
        accept_task(member.get()) {}

  std::unique_ptr<ContestMember> member;
  // Indicates whether m fingers have been on the screen at the same time
  // during the current tap.
  bool tap_in_progress = false;
  // Keeps the count of the number of taps detected so far, for the gesture.
  uint32_t number_of_taps_detected = 0;
  // Indicaes whether the recognizer has successfully accepted the gesture.
  bool won = false;
  // Async task to schedule tap length timeout.
  // This task enforces a timeout between the first DOWN event and last UP event
  // of a particular tap.
  async::TaskClosureMethod<ContestMember, &ContestMember::Reject> tap_length_timeout;
  // Async task used to schedule tap interval timeout.
  // This task enforces a timeout between the last UP event of a tap and the
  // first DOWN event of the next tap.
  async::TaskClosureMethod<ContestMember, &ContestMember::Reject> tap_interval_timeout;
  // Async task to schedule delayed win for held tap.
  async::TaskClosureMethod<ContestMember, &ContestMember::Accept> accept_task;
};

MFingerNTapDragRecognizer::MFingerNTapDragRecognizer(
    OnMFingerNTapDragCallback on_recognize, OnMFingerNTapDragCallback on_update,
    OnMFingerNTapDragCallback on_complete, uint32_t number_of_fingers, uint32_t number_of_taps,
    float drag_displacement_threshold, float update_displacement_threshold)
    : on_recognize_(std::move(on_recognize)),
      on_update_(std::move(on_update)),
      on_complete_(std::move(on_complete)),
      number_of_fingers_in_gesture_(number_of_fingers),
      number_of_taps_in_gesture_(number_of_taps),
      drag_displacement_threshold_(drag_displacement_threshold),
      update_displacement_threshold_(update_displacement_threshold) {}

MFingerNTapDragRecognizer::~MFingerNTapDragRecognizer() = default;

void MFingerNTapDragRecognizer::OnTapStarted() {
  // If this tap is the last in the gesture, post a task to accept the gesture
  // if the fingers are still on screen after kMinTapHoldDuration has elapsed.
  // Otherwise, if this tap is NOT the last in the gesture, post a task to
  // reject the gesture if the fingers have not lifted by the time kTapTimeout
  // elapses. In this case, we also need to cancel the tap length timeout.
  if (contest_->number_of_taps_detected == number_of_taps_in_gesture_ - 1) {
    contest_->tap_length_timeout.Cancel();
    contest_->accept_task.PostDelayed(async_get_default_dispatcher(), kMinTapHoldDuration);
  }
}

void MFingerNTapDragRecognizer::OnExcessFingers() {
  // If the gesture has already been accepted (i.e. the user has successfully
  // performed n-1 taps, followed by a valid hold, but an (m+1)th finger comes
  // down on screen, we should invoke the on_complete_ callback.
  // In any event, the gesture is no longer valid, so we should reset the
  // recognizer.
  if (contest_->won) {
    on_complete_(gesture_context_);
  }

  ResetRecognizer();
}

void MFingerNTapDragRecognizer::OnMoveEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // If we've accepted the gesture, invoke on_update_. Otherwise, if the current
  // tap is the last (which could become a drag), we should check if the
  // fingers have already moved far enough to constitute a drag.
  // If this tap is not the last, we should verify that the
  // fingers are close enough to their starting locations to constitute a valid
  // tap.
  if (contest_->won) {
    if (DisplacementExceedsThreshold(
            last_update_gesture_context_.CurrentCentroid(/* use_local_coordinates = */ false),
            gesture_context_.CurrentCentroid(/* use_local_coordinates = */ false),
            update_displacement_threshold_)) {
      on_update_(gesture_context_);
      last_update_gesture_context_ = gesture_context_;
    }
  } else if (contest_->number_of_taps_detected == number_of_taps_in_gesture_ - 1) {
    if (DisplacementExceedsThreshold(
            gesture_context_.StartingCentroid(/* use_local_coordinates = */ false),
            gesture_context_.CurrentCentroid(/* use_local_coordinates = */ false),
            drag_displacement_threshold_)) {
      contest_->member->Accept();
      return;
    }
  } else if (!PointerEventIsValidTap(gesture_context_, pointer_event)) {
    ResetRecognizer();
  }
}

void MFingerNTapDragRecognizer::OnUpEvent() {
  // If we've already accepted the gesture, then we should invoke on_complete_
  // and reset the recognizer once the first UP event is received (at which
  // point, the drag is considered complete).
  if (contest_->won) {
    on_complete_(gesture_context_);
    ResetRecognizer();
    return;
  }

  // If we have counted number_of_taps_in_gesture_ - 1 complete taps, then this
  // UP event must mark the end of the drag. If we have not already accepted the
  // gesture at this point, we should reject.
  if (contest_->number_of_taps_detected == number_of_taps_in_gesture_ - 1) {
    ResetRecognizer();
    return;
  }

  // If this UP event removed the last finger from the screen, then the most
  // recent tap is complete.
  if (!NumberOfFingersOnScreen(gesture_context_)) {
    // If we've made it this far, we know that (1) m fingers were on screen
    // simultaneously during the current single tap, and (2) The m fingers have
    // now been removed, without any interceding finger DOWN events.
    // Therefore, we can conclude that a complete m-finger tap has occurred.
    contest_->number_of_taps_detected++;

    // Mark that all m fingers were removed from the screen.
    contest_->tap_in_progress = false;

    // Cancel task which was scheduled for detecting single tap.
    contest_->tap_length_timeout.Cancel();

    // Schedule task with delay of timeout_between_taps_.
    contest_->tap_interval_timeout.PostDelayed(async_get_default_dispatcher(), kTimeoutBetweenTaps);
  }
}

void MFingerNTapDragRecognizer::HandleEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_DCHECK(contest_);
  FX_DCHECK(pointer_event.has_pointer_id())
      << DebugName() << ": Pointer event is missing pointer id.";
  const auto pointer_id = pointer_event.pointer_id();
  FX_DCHECK(pointer_event.has_phase())
      << DebugName() << ": Pointer event is missing phase information.";
  switch (pointer_event.phase()) {
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      // If we receive a DOWN event when there are already m fingers on the
      // screen, then either we've received a second DOWN event for one of the fingers that's
      // already on the screen, or we've received a DOWN event for an (m+1)th
      // finger. In either case, we should abandon the current gesture.
      if (NumberOfFingersOnScreen(gesture_context_) >= number_of_fingers_in_gesture_) {
        OnExcessFingers();
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
        ResetRecognizer();
        break;
      }

      //  If we receive successive DOWN events for the same pointer without an
      //  UP event, then we should abandon the current gesture.
      if (FingerIsOnScreen(gesture_context_, pointer_id)) {
        ResetRecognizer();
        break;
      }

      // Initialize starting info for this new tap.
      if (!InitializeStartingGestureContext(pointer_event, &gesture_context_)) {
        ResetRecognizer();
        break;
      }

      // If the total number of fingers involved in the gesture now exceeds
      // number_of_fingers_in_gesture_, reject the gesture.
      if (gesture_context_.starting_pointer_locations.size() > number_of_fingers_in_gesture_) {
        ResetRecognizer();
        break;
      }

      // On the first DOWN event of the tap, cancel the tap interval timeout and
      // schedule the tap length timeout.
      if (NumberOfFingersOnScreen(gesture_context_) == 1) {
        contest_->tap_interval_timeout.Cancel();
        contest_->tap_length_timeout.PostDelayed(async_get_default_dispatcher(), kTapTimeout);
      }

      contest_->tap_in_progress =
          (NumberOfFingersOnScreen(gesture_context_) == number_of_fingers_in_gesture_);
      // Only start the timeout once all m fingers are on the screen together.
      if (contest_->tap_in_progress) {
        OnTapStarted();
      }

      break;

    case fuchsia::ui::input::PointerEventPhase::MOVE:
      FX_DCHECK(FingerIsOnScreen(gesture_context_, pointer_id))
          << DebugName() << ": Pointer MOVE event received without preceding DOWN event.";

      // Validate the pointer_event for the gesture being performed.
      if (!ValidatePointerEvent(gesture_context_, pointer_event)) {
        ResetRecognizer();
        break;
      }

      UpdateGestureContext(pointer_event, true /* finger is on screen */, &gesture_context_);

      OnMoveEvent(pointer_event);

      break;

    case fuchsia::ui::input::PointerEventPhase::UP:
      FX_DCHECK(FingerIsOnScreen(gesture_context_, pointer_id))
          << DebugName() << ": Pointer UP event received without preceding DOWN event.";

      // Validate pointer_event for the gesture being performed.
      if (!ValidatePointerEvent(gesture_context_, pointer_event)) {
        ResetRecognizer();
        break;
      }

      UpdateGestureContext(pointer_event, false /* finger is not on screen */, &gesture_context_);

      // The number of fingers on screen during a multi-finger tap should
      // monotonically increase from 0 to m, and
      // then monotonically decrease back to 0. If a finger is removed before
      // number_of_fingers_in_gesture_ fingers are on the screen simultaneously,
      // then we should reject this gesture.
      if (!contest_->tap_in_progress) {
        ResetRecognizer();
        break;
      }

      OnUpEvent();

      break;
    default:
      break;
  }
}

bool MFingerNTapDragRecognizer::DisplacementExceedsThreshold(::fuchsia::math::PointF start,
                                                             ::fuchsia::math::PointF end,
                                                             float threshold) {
  return SquareDistanceBetweenPoints(start, end) >= threshold * threshold;
}

void MFingerNTapDragRecognizer::ResetRecognizer() {
  contest_.reset();
  ResetGestureContext(&gesture_context_);
}

void MFingerNTapDragRecognizer::OnWin() {
  on_recognize_(gesture_context_);
  last_update_gesture_context_ = gesture_context_;
  if (contest_) {
    contest_->won = true;
  } else {
    // It's possible that we don't get awarded the win until after the gesture has
    // completed, in which case we also need to call the complete handler.
    on_complete_(gesture_context_);
    ResetRecognizer();
  }
}

void MFingerNTapDragRecognizer::OnDefeat() { ResetRecognizer(); }

void MFingerNTapDragRecognizer::OnContestStarted(std::unique_ptr<ContestMember> contest_member) {
  ResetRecognizer();
  contest_ = std::make_unique<Contest>(std::move(contest_member));
}

std::string MFingerNTapDragRecognizer::DebugName() const {
  return fxl::StringPrintf("MFingerNTapDragRecognizer(m=%d, n=%d)", number_of_fingers_in_gesture_,
                           number_of_taps_in_gesture_);
}

}  // namespace a11y
