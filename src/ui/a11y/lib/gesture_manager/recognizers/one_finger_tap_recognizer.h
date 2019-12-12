// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_TAP_RECOGNIZER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_TAP_RECOGNIZER_H_

#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include "src/ui/a11y/lib/gesture_manager/arena/contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace a11y {

// OneFingerTapRecognizer class is responsible for implementing one finger
// single tap gesture.
//
// This gesture is a passive gesture, which means this gesture will not declare itself a winnner.
// The only way this gesture can win in the arena is when this is the last gesture remaining.
// Constructor of this class takes in a callback and a tap timeout. Callback will be called when
// gesture is detected and is also the winner. Tap timeout is the amount of time in which the tap
// gesture should complete.
//
// This class, schedules a delayed task on default dispatcher, when gesture starts. This task
// declares defeat for the current recognizer. The time used for scheduling this task is the tap
// timeout. If gesture is recognized in this timeout period, then the scheduled task is cancelled.
// If not recognized, scheduled task will get executed.
class OneFingerTapRecognizer : public GestureRecognizer {
 public:
  // Max value by which pointer events can move(relative to first point of contact), and still are
  // valid for tap gestures, in NDC.
  static constexpr float kGestureMoveThreshold = 1.f / 16;

  // Maximum time the tap can take.
  static constexpr zx::duration kOneFingerTapTimeout = zx::msec(300);

  // Callback which will be invoked when one finger tap gesture has been recognized.
  using OnOneFingerTap = fit::function<void(GestureContext)>;

  // Tap timeout is the maximum time a finger can be in contact with the screen to be considered a
  // tap. Callback will be invoked when the recognizer wins the gesture arena.
  OneFingerTapRecognizer(OnOneFingerTap callback, zx::duration tap_timeout = kOneFingerTapTimeout);
  ~OneFingerTapRecognizer() override;

  void HandleEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) override;
  void OnWin() override;
  void OnDefeat() override;
  void OnContestStarted(std::unique_ptr<ContestMember> contest_member) override;
  std::string DebugName() const override;

 private:
  // Represents state internal to a contest, i.e. contest member, long-press timeout, and tap state.
  struct Contest;

  // Helper function to check if the provided pointer event is valid for single tap gesture by
  // verifying the move threshold and tap timeout.
  bool ValidatePointerEventForTap(
      const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Stores the Gesture Context which is required to execute the callback.
  GestureContext gesture_context_;

  // Callback which will be executed when gesture is executed.
  OnOneFingerTap one_finger_tap_callback_;

  // Maximum time a tap can take.
  const zx::duration tap_timeout_;

  // GestureInfo which is used to store the initial state of the gesture which is currently being
  // performed.
  GestureInfo gesture_start_info_;

  std::unique_ptr<Contest> contest_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_TAP_RECOGNIZER_H_
