// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_SWIPE_RECOGNIZER_BASE_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_SWIPE_RECOGNIZER_BASE_H_

#include <lib/zx/time.h>

#include <unordered_map>

#include "src/ui/a11y/lib/gesture_manager/arena/contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

namespace a11y {

// SwipeRecognizerBase class is an abstract class that implements most of the
// swipe gesture recognition logic for n fingers where n >= 1.
//
// Swipe gestures are directional (up, down, right, or left), so directional recognizers
// will inherit from this base class and override the ValidateSwipeSlopeAndDirection()
// method, in which the directional differentiation logic is encapsulated.
class SwipeRecognizerBase : public GestureRecognizer {
 public:
  // Minimum distance (in NDC) between finger down and finger up events for gesture to be
  // considered a swipe.
  static constexpr float kMinSwipeDistance = 2.f / 8;

  // Maximum duration of swipe (in milliseconds).
  static constexpr zx::duration kDefaultSwipeGestureTimeout = zx::msec(500);

  static constexpr uint32_t kDefaultNumberOfFingers = 1;

  // Callback which will be invoked when the swipe gesture has been recognized.
  using SwipeGestureCallback = fit::function<void(GestureContext)>;

  // Timeout is the maximum time a finger can be in contact with the screen to be considered a
  // swipe. Callback is invoked when swipe gesture is detected and the recognizer is the winner in
  // gesture arena. |number_of_fingers| is the number of fingers that will be used to perform the
  // swipe gesture.
  SwipeRecognizerBase(SwipeGestureCallback callback, uint32_t number_of_fingers,
                      zx::duration swipe_gesture_timeout, const std::string& debug_name);
  ~SwipeRecognizerBase() override;

  void HandleEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) override;
  void OnWin() override;
  void OnDefeat() override;
  void OnContestStarted(std::unique_ptr<ContestMember> contest_member) override;
  std::string DebugName() const override = 0;

 protected:
  // Swipe gestures are directional (up, down, right, or left). In order to be recognized as a
  // swipe, the slope of the line containing the gesture start and end points must fall within a
  // specified range, which varies based on the direction of the swipe. Furthermore, the slopes of
  // the lines containing each pointer event location and the gesture start point must also fall
  // within this range. If a swipe recognizer receives a pointer event for which this slope property
  // does NOT hold, the recognizer will abandon the gesture. Each directional recognizer must
  // specify the range of acceptable slopes by implementing the method below, which verifies that a
  // given slope value falls within that range.
  virtual bool SwipeHasValidSlopeAndDirection(float x_displacement, float y_displacement) const = 0;

 private:
  // Represents state internal to a contest, i.e. contest member, hold timeout, and tap state.
  struct Contest;

  // Resets contest_ and gesture_context_.
  void ResetRecognizer();

  // Determines whether a gesture's is close enough to up, down, left, or right to be
  // remain in consideration as a swipe. Returns true if so, false otherwise.
  bool ValidateSwipePath(
      uint32_t pointer_id,
      const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const;

  // Checks if the distance between the start and end points of a swipe is more than
  // kMinSwipeDistance.
  bool MinSwipeLengthAchieved(
      uint32_t pointer_id,
      const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) const;

  // Helper function to save GestureInfo for last pointer position.
  void UpdateLastPointerPosition(
      uint32_t pointer_id, const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

  // Callback which will be executed when the gesture is performed.
  SwipeGestureCallback swipe_gesture_callback_;

  // Swipe gesture timeout(in milliseconds). If the gesture is not completed within this time
  // period, then it won't be recognized.
  const zx::duration swipe_gesture_timeout_;

  GestureContext gesture_context_;

  // Number of fingers that will be used to perform the swipe gesture.
  uint32_t number_of_fingers_ = kDefaultNumberOfFingers;

  // String name of the recognizer, to be used in logs only.
  const std::string debug_name_;

  std::unique_ptr<Contest> contest_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_SWIPE_RECOGNIZER_BASE_H_
