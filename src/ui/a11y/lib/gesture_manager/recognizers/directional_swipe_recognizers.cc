// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/recognizers/directional_swipe_recognizers.h"

#include <cmath>

namespace a11y {

bool UpSwipeGestureRecognizer::ValidateSwipeSlopeAndDirection(float x_displacement,
                                                              float y_displacement) {
  // If y_displacement is negative, then this gesture cannot be "up".
  if (y_displacement <= 0) {
    return false;
  }

  // If y_displacement exceeds |x_displacement| by a factor of at least kMinUpSwipeSlopeMagnitude,
  // then gesture is sufficiently vertical to be an "up" swipe. Note that this check is equivalent
  // to checking if swipe_slope >= kMinUpSwipeSlopeMagnitude, except that this check additionally
  // accounts for the case in which the line is vertical (i.e. x_displacement == 0), in which we
  // cannot compute the slope of the swipe path.
  return y_displacement >= std::abs(x_displacement) * kMinUpSwipeSlopeMagnitude;
}

bool DownSwipeGestureRecognizer::ValidateSwipeSlopeAndDirection(float x_displacement,
                                                                float y_displacement) {
  // If y_displacement is positive, then this gesture cannot be "down".
  if (y_displacement >= 0) {
    return false;
  }

  // If |y_displacement| exceeds |x_displacement| by a factor of at least
  // kMinDownSwipeSlopeMagnitude, then gesture is sufficiently vertical to be a "down" swipe. Note
  // that this check is equivalent to checking if swipe_slope >= kMinDownSwipeSlopeMagnitude, except
  // that this check additionally accounts for the case in which the line is vertical (i.e.
  // x_displacement == 0), in which we cannot compute the slope of the swipe path.
  return std::abs(y_displacement) >= std::abs(x_displacement) * kMinDownSwipeSlopeMagnitude;
}

bool RightSwipeGestureRecognizer::ValidateSwipeSlopeAndDirection(float x_displacement,
                                                                 float y_displacement) {
  // If x_displacement is negative, then this gesture cannot be "right".
  if (x_displacement <= 0) {
    return false;
  }

  // If |y_displacement| is no more than x_displacement * kMaxRightSwipeSlopeMagnitude, then
  // gesture is sufficiently horizontal to be a "right" swipe.
  return std::abs(y_displacement) <= x_displacement * kMaxRightSwipeSlopeMagnitude;
}

bool LeftSwipeGestureRecognizer::ValidateSwipeSlopeAndDirection(float x_displacement,
                                                                float y_displacement) {
  // If x_displacement is positive, then this gesture cannot be "left".
  if (x_displacement >= 0) {
    return false;
  }

  // If |y_displacement| is no more than |x_displacement| * kMaxLeftSwipeSlopeMagnitude, then
  // gesture is sufficiently horizontal to be a "right" swipe.
  return std::abs(y_displacement) <= std::abs(x_displacement) * kMaxLeftSwipeSlopeMagnitude;
}

}  // namespace a11y
