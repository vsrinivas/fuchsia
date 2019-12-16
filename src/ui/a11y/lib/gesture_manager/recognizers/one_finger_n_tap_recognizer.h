// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_N_TAP_RECOGNIZER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_N_TAP_RECOGNIZER_H_

#include "src/ui/a11y/lib/gesture_manager/arena/contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace a11y {
// OneFingerNTapRecognizer class is responsible for implementing one finger N tap gesture.
class OneFingerNTapRecognizer : public GestureRecognizer {
 public:
  // Default value for maximum time the tap can take.
  static constexpr zx::duration kTapTimeout = zx::msec(300);

  // Default value for maximum time under which the next tap should start.
  static constexpr zx::duration kTimeoutBetweenTaps = zx::msec(250);

  // Callback which will be invoked when gesture has been recognized.
  using OnFingerTapGesture = fit::function<void(GestureContext)>;

  // Constructor of this class takes in following parameters:
  //  1. callback: Callback will be invoked, when gesture is detected and the recognizer
  //     is the winner in gesture arena.
  //  2. number_of_taps: Number of tap gesture recognizer will detect.
  //  3. tap_timeout: Tap timeout is the maximum time a finger can be in contact with the screen to
  //     be considered a tap.
  //  4. timeout_between_taps: Timeout between taps is the maximum time that is allowed between the
  //     end of first tap and the start of the other. Callback will be invoked, when gesture is
  //     detected and the recognizer is the winner in gesture arena.
  // When the gesture starts, we schedule a timeout on the default dispatcher. If gesture is
  // recognized in this timeout period, then the scheduled task is cancelled. If not recognized,
  // scheduled tasks will get executed which will declare defeat for the current recognizer.
  OneFingerNTapRecognizer(OnFingerTapGesture callback, int number_of_taps,
                          zx::duration tap_timeout = kTapTimeout,
                          zx::duration timeout_between_taps = kTimeoutBetweenTaps);

  ~OneFingerNTapRecognizer() override;

  // A human-readable string name for the recognizer to be used in logs only.
  std::string DebugName() const override;

  // Processes incoming pointer events to detect tap gestures like (Single, double, etc.).
  void HandleEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) override;

  // This method gets called when the recognizer has won the arena.
  void OnWin() override;

  // This method gets called when the recognizer has lost the arena.
  // It resets the state of the contest member.
  void OnDefeat() override;

  // At the start of every arena contest this method will be called.
  // This also resets the state of the recognizer.
  void OnContestStarted(std::unique_ptr<ContestMember> contest_member) override;

 private:
  // Represents state internal to a contest, i.e. contest member, long-press timeout, and tap state.
  struct Contest;

  // Contains validation logic which is needed for PointerEvent.
  bool ValidateEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const;

  // Helper function to check if the provided pointer event is valid for current tap gesture being
  // performed, by verifying the move threshold.
  bool ValidatePointerEventForTap(
      const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const;

  // Checks if required number of taps are recognized.
  bool CheckIfGestureIsDetected() const;

  // Stores the Gesture Context which is required to execute the callback.
  GestureContext gesture_context_;

  // Callback which will be executed when gesture is detected and is also a winner in the arena.
  OnFingerTapGesture on_finger_tap_callback_;

  // Number of taps this gesture recognizer will detect.
  const int number_of_taps_in_gesture_;

  // Maximum time a tap can take.
  const zx::duration tap_timeout_;

  // Maximum time under which the next tap should start.
  const zx::duration timeout_between_taps_;

  // GestureInfo which is used to store the initial state of the gesture which is currently
  // being performed.
  GestureInfo gesture_start_info_;

  // Pointer to Contest which is required to perform operations like reset() or ScheduleTask.
  std::unique_ptr<Contest> contest_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_N_TAP_RECOGNIZER_H_
