// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_TAP_RECOGNIZER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_TAP_RECOGNIZER_H_

#include <lib/async/cpp/task.h>

#include "lib/zx/time.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

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
// timeout. If gesture is recognized in this timeout period, then the scehduled task is cancelled.
// If not recognized, scheduled task will get executed.
class OneFingerTapRecognizer : public GestureRecognizer {
 public:
  // Various states of Gesture Recognizer state machine.
  enum TapGestureState {
    kNotStarted,
    kInProgress,
    kDownFingerDetected,
    kGestureDetectedAndWaiting
  };

  // Struct for holding context(Koid, location) about Gesture.
  struct GestureContext {
    zx_koid_t view_ref_koid;
    ::fuchsia::math::PointF local_point;
  };

  // Struct for holding initial information about Gesture under consideration.
  struct GestureInfo {
    uint64_t gesture_start_time;
    uint32_t device_id;
    uint32_t pointer_id;
    ::fuchsia::math::PointF starting_global_position;
    ::fuchsia::math::PointF starting_local_position;
    zx_koid_t view_ref_koid;
  };

  // Max value by which pointer events can move(relative to first point of contact), and still are
  // valid for tap gestures.
  static constexpr uint32_t kGestureMoveThreshold = 8;

  // Time(in milli seconds) under which one finger tap gesture should be performed.
  static constexpr uint32_t kOneFingerTapTimeout = 300;

  // Callback which will be invoked when one finger tap gesture has been recognized.
  using OnOneFingerTap = fit::function<void(GestureContext)>;

  // Tap timeout(in milli second) is the maximum time a finger can be in contact with the screen to
  // be considered a tap.
  // Callback will be invoked, when gesture is detected and the recognizer is the winner in gesture
  // arena.
  OneFingerTapRecognizer(OnOneFingerTap callback, uint64_t tap_timeout = kOneFingerTapTimeout);

  // Initializes pointer to Arena Member.
  void AddArenaMember(ArenaMember* new_arena_member);

  // Processes incoming pointer events to detect single tap gesture.
  void HandleEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) override;

  // This method gets called when the recognizer has won the arena.
  // If gesture is already detected then calls the callback.
  // If gesture is not yet detected then, mark the state of gesture recognizer as won and waiting
  // for the gesture to complete.
  void OnWin() override;

  // This method gets called when the recognizer has lost the arena.
  // It resets the state of the recognizer.
  void OnDefeat() override;

  // A human-readable string name for the recognizer to be used in logs only.
  std::string DebugName() override;

  // Returns current state of the gesture recognizer.
  TapGestureState GetGestureState() { return gesture_state_; }

 private:
  // Helper function to schedule a DeclareDefeat() task with a timeout.
  void ScheduleCallbackTask();

  // Helper function to cancel DeclareDefeat() task.
  void CancelCallbackTask();

  // Helper function to Reset the state of all the variables.
  void ResetState();

  // Helper function to initialize GestureInfo using the provided pointer_event.
  bool InitGestureInfo(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Helper function to make sure provided pointer_event is valid to be processed for the current
  // gesture.
  bool ValidatePointerEvent(
      const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const;

  // Helper function which will be executed when recognizer is a winner and gesture is detected.
  // It also calls OnOneFingerTap() callback along with notifying GestureArena to Stop sending
  // pointer events.
  void ExecuteOnWin();

  // Helper function to either call DeclareDefeat or StopRoutingPpointerEvents based on the state of
  // the recognizer.
  void AbandonGesture();

  // Stores the current state of the Gesture State Machine.
  TapGestureState gesture_state_ = kNotStarted;

  // Stores the pointer to Arena Member.
  ArenaMember* arena_member_ = nullptr;

  // Stores the Gesture Context which is required to execute the callback.
  GestureContext gesture_context_;

  // Callback which will be executed when gesture is executed.
  OnOneFingerTap one_finger_tap_callback_;

  // One finger tap timeout(in mili seconds), if the gesture is performed over a time longer than
  // this timeout then it won't be recognized.
  uint64_t one_finger_tap_timeout_;

  // Used for performing delayed task.
  async::Task gesture_task_;

  // Flag to declare if GestureArena has declared this recognizer a winner.
  bool is_winner_ = false;

  // GestureInfo which is used to store the initial state of the gesture which is currently being
  // performed.
  GestureInfo gesture_start_info_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ONE_FINGER_TAP_RECOGNIZER_H_
