// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_M_FINGER_N_TAP_RECOGNIZER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_M_FINGER_N_TAP_RECOGNIZER_H_

#include "src/ui/a11y/lib/gesture_manager/arena/contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace a11y {
// MFingerNTapRecognizer class is responsible for implementing m-finger-n-tap gesture.
class MFingerNTapRecognizer : public GestureRecognizer {
 public:
  // Default value for maximum time the tap can take.
  static constexpr zx::duration kTapTimeout = zx::msec(300);

  // Default value for maximum time under which the next tap should start.
  static constexpr zx::duration kTimeoutBetweenTaps = zx::msec(250);

  // Callback which will be invoked when gesture has been recognized.
  using OnMFingerNTapCallback = fit::function<void(GestureContext)>;

  // Constructor of this class takes in following parameters:
  //  1. callback: Callback will be invoked, when the gesture is detected and the recognizer
  //     is the winner in gesture arena.
  //  2. number_of_fingers: Number of fingers in gesture.
  //  3. number_of_taps: Number of taps gesture recognizer will detect.
  // When the gesture starts, we schedule a timeout on the default dispatcher. If gesture is
  // recognized in this timeout period, then the scheduled task is cancelled. If not recognized,
  // scheduled tasks will get executed which will declare defeat for the current recognizer.
  MFingerNTapRecognizer(OnMFingerNTapCallback callback, uint32_t number_of_fingers,
                        uint32_t number_of_taps);

  ~MFingerNTapRecognizer() override;

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
  struct Contest;

  // Helper method invoked when more than m fingers are in contact with the
  // screen.
  virtual void OnExcessFingers();

  // Helper method invoked for valid MOVE events.
  virtual void OnMoveEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Helper method invoked for valid UP events.
  virtual void OnUpEvent();

  // Resets contest_ and gesture_context_.
  virtual void ResetRecognizer();

  // Stores the Gesture Context which is required to execute the callback.
  GestureContext gesture_context_;

  // Callback which will be executed when gesture is detected and is also a winner in the arena.
  OnMFingerNTapCallback on_recognize_;

  // Number of fingers in gesture.
  const uint32_t number_of_fingers_in_gesture_;

  // Number of taps this gesture recognizer will detect.
  const uint32_t number_of_taps_in_gesture_;

  // Pointer to Contest which is required to perform operations like reset() or ScheduleTask.
  std::unique_ptr<Contest> contest_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_M_FINGER_N_TAP_RECOGNIZER_H_
