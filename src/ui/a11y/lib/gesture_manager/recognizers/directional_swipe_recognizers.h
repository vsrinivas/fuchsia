// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_DIRECTIONAL_SWIPE_RECOGNIZERS_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_DIRECTIONAL_SWIPE_RECOGNIZERS_H_

#include "lib/zx/time.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/swipe_recognizer_base.h"

namespace a11y {

class UpSwipeGestureRecognizer : public SwipeRecognizerBase {
 public:
  // A line with a slope of 1.732 has an angle of elevation above the x-axis of ~60 degrees,
  // so in order for a swipe to be recognized as "up", it must fall within 30 degrees of vertical.
  static constexpr float kMinUpSwipeSlopeMagnitude = 1.732f;

  UpSwipeGestureRecognizer(
      SwipeGestureCallback callback,
      zx::duration swipe_gesture_timeout = SwipeRecognizerBase::kSwipeGestureTimeout)
      : SwipeRecognizerBase(std::move(callback), swipe_gesture_timeout) {}

  std::string DebugName() const override { return "up_swipe_gesture_recognizer"; };

 protected:
  // Verifies that the absolute value of the slope of the line containing the gesture start point
  // and the location of the pointer event in question is sufficiently large (i.e. the swipe is
  // "vertical"), and that y_displacement is positive (i.e. the swipe is "up").
  bool ValidateSwipeSlopeAndDirection(float x_displacement, float y_displacement) override;
};

class DownSwipeGestureRecognizer : public SwipeRecognizerBase {
 public:
  // A line with a slope of 1.732 has an angle of elevation above the x-axis of ~60 degrees,
  // so in order for a swipe to be recognized as "down", it must fall within 30 degrees of vertical.
  static constexpr float kMinDownSwipeSlopeMagnitude = 1.732f;

  DownSwipeGestureRecognizer(
      SwipeGestureCallback callback,
      zx::duration swipe_gesture_timeout = SwipeRecognizerBase::kSwipeGestureTimeout)
      : SwipeRecognizerBase(std::move(callback), swipe_gesture_timeout) {}

  std::string DebugName() const override { return "down_swipe_gesture_recognizer"; };

 protected:
  // Verifies that the absolute value of the slope of the line containing the gesture start point
  // and the location of the pointer event in question is sufficiently large (i.e. the swipe is
  // "vertical"), and that y_displacement is negative (i.e. the swipe is "down").
  bool ValidateSwipeSlopeAndDirection(float x_displacement, float y_displacement) override;
};

class RightSwipeGestureRecognizer : public SwipeRecognizerBase {
 public:
  // A line with a slope of 0.577 has an angle of elevation above the x-axis of ~30 degrees,
  // so in order for a swipe to be recognized as "right", it must fall within 30 degrees of
  // horizontal.
  static constexpr float kMaxRightSwipeSlopeMagnitude = 0.577f;

  RightSwipeGestureRecognizer(
      SwipeGestureCallback callback,
      zx::duration swipe_gesture_timeout = SwipeRecognizerBase::kSwipeGestureTimeout)
      : SwipeRecognizerBase(std::move(callback), swipe_gesture_timeout) {}

  std::string DebugName() const override { return "right_swipe_gesture_recognizer"; };

 protected:
  // Verifies that the absolute value of the slope of the line containing the gesture start point
  // and the location of the pointer event in question is sufficiently small (i.e. the swipe is
  // "horizontal"), and that x_displacement is positive (i.e. the swipe is "right").
  bool ValidateSwipeSlopeAndDirection(float x_displacement, float y_displacement) override;
};

class LeftSwipeGestureRecognizer : public SwipeRecognizerBase {
 public:
  // A line with a slope of 0.577 has an angle of elevation above the x-axis of ~30 degrees,
  // so in order for a swipe to be recognized as "left", it must fall within 30 degrees of
  // horizontal.
  static constexpr float kMaxLeftSwipeSlopeMagnitude = 0.577f;

  LeftSwipeGestureRecognizer(
      SwipeGestureCallback callback,
      zx::duration swipe_gesture_timeout = SwipeRecognizerBase::kSwipeGestureTimeout)
      : SwipeRecognizerBase(std::move(callback), swipe_gesture_timeout) {}

  std::string DebugName() const override { return "left_swipe_gesture_recognizer"; };

 protected:
  // Verifies that the absolute value of the slope of the line containing the gesture start point
  // and the location of the pointer event in question is sufficiently small (i.e. the swipe is
  // "horizontal"), and that x_displacement is left (i.e. the swipe is "left").
  bool ValidateSwipeSlopeAndDirection(float x_displacement, float y_displacement) override;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_DIRECTIONAL_SWIPE_RECOGNIZERS_H_
