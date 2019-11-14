// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_SWIPE_RECOGNIZER_BASE_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_SWIPE_RECOGNIZER_BASE_H_

#include <lib/async/cpp/task.h>

#include "lib/zx/time.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace a11y {

// SwipeRecognizerBase class is an abstract class that implements most of the
// swipe gesture recognition logic.
//
// Swipe gestures are directional (up, down, right, or left), so directional recognizers
// will inherit from this base class and override the ValidateSwipeSlopeAndDirection()
// method, in which the directional differentiation logic is encapsulated.
class SwipeRecognizerBase : public GestureRecognizer {
 public:
  // Various states of Gesture Recognizer state machine.
  enum class SwipeGestureState { kNotStarted, kDownFingerDetected, kGestureDetected, kDone };

  // Minimum distance (in NDC) between finger down and finger up events for gesture to be
  // considered a swipe.
  static constexpr float kMinSwipeDistance = 3.f / 8;

  // Max distance (in NDC) between finger down and finger up events for gesture to be considered
  // a swipe.
  static constexpr float kMaxSwipeDistance = 3.f / 4;

  // Maximum duration of swipe (in milliseconds).
  static constexpr zx::duration kSwipeGestureTimeout = zx::msec(500);

  // Callback which will be invoked when swipe gesture has been recognized.
  using SwipeGestureCallback = fit::function<void(GestureContext)>;

  // Timeout is the maximum time a finger can be in contact with the screen to be considered a
  // swipe. Callback is invoked when swipe gesture is detected and the recognizer is the winner in
  // gesture arena.
  SwipeRecognizerBase(SwipeGestureCallback callback, zx::duration swipe_gesture_timeout);

  // Initializes pointer to Arena Member.
  void AddArenaMember(ArenaMember* new_arena_member);

  // Processes incoming pointer events.
  void HandleEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) override;

  // This method gets called when the recognizer has won the arena.
  // If gesture is already detected then calls the callback.
  // If gesture is not yet detected then, mark the state of gesture recognizer as won and waiting
  // for the gesture to complete.
  void OnWin() override;

  // This method gets called when the recognizer has lost the arena.
  // It resets the state of the recognizer.
  void OnDefeat() override;

  void OnContestStarted() override;

  // A human-readable string name for the recognizer to be used in logs only.
  std::string DebugName() const override = 0;

  // Returns current state of the gesture recognizer.
  SwipeGestureState GetGestureState() { return gesture_state_; }

 protected:
  // Swipe gestures are directional (up, down, right, or left). In order to be recognized as a
  // swipe, the slope of the line containing the gesture start and end points must fall within a
  // specified range, which varies based on the direction of the swipe. Furthermore, the slopes of
  // the lines containing each pointer event location and the gesture start point must also fall
  // within this range. If a swipe recognizer receives a pointer event for which this slope property
  // does NOT hold, the recognizer will abandon the gesture. Each directional recognizer must
  // specify the range of acceptable slopes by implementing the method below, which verifies that a
  // given slope value falls within that range.
  virtual bool ValidateSwipeSlopeAndDirection(float x_displacement, float y_displacement) = 0;

  // Helper function to Reset the state of all the variables.
  void ResetState();

  // Helper function which will be executed when recognizer is a winner.
  // Calls swipe_gesture_callback_ on gesture_context_ at time win is declared.
  void ExecuteOnWin();

  // Helper function to reject a gesture in the arena and reset the state of the recognizer.
  void AbandonGesture();

  // Determines whether a gesture's is close enough to up, down, left, or right to be
  // remain in consideration as a swipe. Returns true if so, false otherwise.
  bool ValidateSwipePath(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Checks if the distance between the start and end points of a swipe fall within the accepted
  // range.
  bool ValidateSwipeDistance(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Stores the current state of the Gesture State Machine.
  SwipeGestureState gesture_state_ = SwipeGestureState::kNotStarted;

  // Stores the Gesture Context which is required to execute the callback.
  GestureContext gesture_context_;

  // Callback which will be executed when gesture is executed.
  SwipeGestureCallback swipe_gesture_callback_;

  // Async task used to scheduled gesture timeout.
  async::TaskClosureMethod<SwipeRecognizerBase, &SwipeRecognizerBase::AbandonGesture> abandon_task_;

  // Swipe gesture timeout(in mili seconds). If the gesture is not completed within this time
  // period, then it won't be recognized.
  const zx::duration swipe_gesture_timeout_;

  // Flag to declare if GestureArena has declared this recognizer a winner.
  bool is_winner_ = false;

  // GestureInfo which is used to store the initial state of the gesture which is currently being
  // performed.
  GestureInfo gesture_start_info_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_SWIPE_RECOGNIZER_BASE_H_
