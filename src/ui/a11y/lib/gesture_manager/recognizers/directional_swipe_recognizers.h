// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_DIRECTIONAL_SWIPE_RECOGNIZERS_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_DIRECTIONAL_SWIPE_RECOGNIZERS_H_

#include "lib/zx/time.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/swipe_recognizer_base.h"

namespace a11y {

// NOTE: These recognizers use normalized device coordinates. This coordinate space defines
// the origin as the center of the screen, with +x extending to the right and +y extending down.

// Recognizer for upward-oriented swipes.
// In NDC, an upward swipe moves toward -y.
class UpSwipeGestureRecognizer : public SwipeRecognizerBase {
 public:
  // A line with a slope of 1.732 has an angle of elevation above the x-axis of ~60 degrees,
  // so in order for a swipe to be recognized as "up", it must fall within 30 degrees of vertical.
  static constexpr float kMinUpSwipeSlopeMagnitude = 1.732f;

  static constexpr char kUpSwipeRecognizerName[] = "Up Swipe Gesture Recognizer";

  explicit UpSwipeGestureRecognizer(
      SwipeGestureCallback callback,
      uint32_t number_of_finger = SwipeRecognizerBase::kDefaultNumberOfFingers,
      zx::duration swipe_gesture_timeout = SwipeRecognizerBase::kDefaultSwipeGestureTimeout)
      : SwipeRecognizerBase(
            std::move(callback), number_of_finger, swipe_gesture_timeout,
            fxl::StringPrintf("%d Finger %s", number_of_finger, kUpSwipeRecognizerName)),
        debug_name_(fxl::StringPrintf("%d Finger %s", number_of_finger, kUpSwipeRecognizerName)) {}

  std::string DebugName() const override { return debug_name_; }

 protected:
  // Verifies that the absolute value of the slope of the line containing the gesture start point
  // and the location of the pointer event in question is sufficiently large (i.e. the swipe is
  // "vertical"), and that y_displacement is positive (i.e. the swipe is "up").
  bool SwipeHasValidSlopeAndDirection(float x_displacement, float y_displacement) const override;

 private:
  // String name of the recognizer to be used in logs only.
  const std::string debug_name_;
};

// Recognizer for downward-oriented swipes.
// In the NDC coordinate space, a downward swipe moves toward +y.
class DownSwipeGestureRecognizer : public SwipeRecognizerBase {
 public:
  // A line with a slope of -1.2 has an angle of elevation below the x-axis of ~50 degrees,
  // so in order for a swipe to be recognized as "down", it must fall within 40 degrees of vertical.
  static constexpr float kMinDownSwipeSlopeMagnitude = 1.2f;

  static constexpr char kDownSwipeRecognizerName[] = "Down Swipe Gesture Recognizer";

  explicit DownSwipeGestureRecognizer(
      SwipeGestureCallback callback,
      uint32_t number_of_finger = SwipeRecognizerBase::kDefaultNumberOfFingers,
      zx::duration swipe_gesture_timeout = SwipeRecognizerBase::kDefaultSwipeGestureTimeout)
      : SwipeRecognizerBase(
            std::move(callback), number_of_finger, swipe_gesture_timeout,
            fxl::StringPrintf("%d Finger %s", number_of_finger, kDownSwipeRecognizerName)),
        debug_name_(fxl::StringPrintf("%d Finger %s", number_of_finger, kDownSwipeRecognizerName)) {
  }

  std::string DebugName() const override { return debug_name_; }

 protected:
  // Verifies that the absolute value of the slope of the line containing the gesture start point
  // and the location of the pointer event in question is sufficiently large (i.e. the swipe is
  // "vertical"), and that y_displacement is negative (i.e. the swipe is "down").
  bool SwipeHasValidSlopeAndDirection(float x_displacement, float y_displacement) const override;

 private:
  // String name of the recognizer to be used in logs only.
  const std::string debug_name_;
};

// Recognizer for right-oriented swipes.
// In the NDC coordinate space, a rightward swipe moves toward +x.
class RightSwipeGestureRecognizer : public SwipeRecognizerBase {
 public:
  // A line with a slope of 0.577 has an angle of elevation above the x-axis of ~30 degrees,
  // so in order for a swipe to be recognized as "right", it must fall within 30 degrees of
  // horizontal.
  static constexpr float kMaxRightSwipeSlopeMagnitude = 0.577f;

  static constexpr char kRightSwipeRecognizerName[] = "Right Swipe Gesture Recognizer";

  explicit RightSwipeGestureRecognizer(
      SwipeGestureCallback callback,
      uint32_t number_of_finger = SwipeRecognizerBase::kDefaultNumberOfFingers,
      zx::duration swipe_gesture_timeout = SwipeRecognizerBase::kDefaultSwipeGestureTimeout)
      : SwipeRecognizerBase(
            std::move(callback), number_of_finger, swipe_gesture_timeout,
            fxl::StringPrintf("%d Finger %s", number_of_finger, kRightSwipeRecognizerName)),
        debug_name_(
            fxl::StringPrintf("%d Finger %s", number_of_finger, kRightSwipeRecognizerName)) {}

  std::string DebugName() const override { return debug_name_; }

 protected:
  // Verifies that the absolute value of the slope of the line containing the gesture start point
  // and the location of the pointer event in question is sufficiently small (i.e. the swipe is
  // "horizontal"), and that x_displacement is positive (i.e. the swipe is "right").
  bool SwipeHasValidSlopeAndDirection(float x_displacement, float y_displacement) const override;

 private:
  // String name of the recognizer to be used in logs only.
  const std::string debug_name_;
};

// Recognizer for left-oriented swipes.
// In the NDC coordinate space, a leftward swipe moves toward -x.
class LeftSwipeGestureRecognizer : public SwipeRecognizerBase {
 public:
  // A line with a slope of 0.577 has an angle of elevation above the x-axis of ~30 degrees,
  // so in order for a swipe to be recognized as "left", it must fall within 30 degrees of
  // horizontal.
  static constexpr float kMaxLeftSwipeSlopeMagnitude = 0.577f;

  static constexpr char kLeftSwipeRecognizerName[] = "Left Swipe Gesture Recognizer";

  explicit LeftSwipeGestureRecognizer(
      SwipeGestureCallback callback,
      uint32_t number_of_finger = SwipeRecognizerBase::kDefaultNumberOfFingers,
      zx::duration swipe_gesture_timeout = SwipeRecognizerBase::kDefaultSwipeGestureTimeout)
      : SwipeRecognizerBase(
            std::move(callback), number_of_finger, swipe_gesture_timeout,
            fxl::StringPrintf("%d Finger %s", number_of_finger, kLeftSwipeRecognizerName)),
        debug_name_(fxl::StringPrintf("%d Finger %s", number_of_finger, kLeftSwipeRecognizerName)) {
  }

  std::string DebugName() const override { return debug_name_; }

 protected:
  // Verifies that the absolute value of the slope of the line containing the gesture start point
  // and the location of the pointer event in question is sufficiently small (i.e. the swipe is
  // "horizontal"), and that x_displacement is left (i.e. the swipe is "left").
  bool SwipeHasValidSlopeAndDirection(float x_displacement, float y_displacement) const override;

 private:
  // String name of the recognizer to be used in logs only.
  const std::string debug_name_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_DIRECTIONAL_SWIPE_RECOGNIZERS_H_
