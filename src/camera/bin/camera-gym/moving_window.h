// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_CAMERA_GYM_MOVING_WINDOW_H_
#define SRC_CAMERA_BIN_CAMERA_GYM_MOVING_WINDOW_H_

#include <fuchsia/math/cpp/fidl.h>

namespace camera {

// MovingWindow is essentially a RectF with motion vectors for each side of the rectangle. The
// motion vectors simply horizontal or vertical, depending on which side of the rectangle. Each
// call to NextWindow() returns a newly moved window position as a RectF, intended for use as the
// parameter to fuchsia::camera3::StreamPtr->SetCropRegion().
//
// The simplest example is to initialize the position to:
//
//   curr_window_.left   = 0.000;
//   curr_window_.right  = 0.500;
//   curr_window_.top    = 0.000;
//   curr_window_.bottom = 0.500;
//
// and the motion vectors to:
//
//   left_inc_      = 0.010;
//   right_inc_     = 0.010;
//   top_inc_       = 0.010;
//   bottom_inc_    = 0.010;
//
// This example will start the region of interest at the upper left corner, move toward the middle
// of the right side, turn and move toward the lower left corner, and so forth until it hits all 4
// sides and all 4 corners, and then repeat. This example leaves the rectange the same size.
//
// The second example is to initialize the position to:
//
//   curr_window_.left   = 0.000;
//   curr_window_.right  = 0.500;
//   curr_window_.top    = 0.000;
//   curr_window_.bottom = 0.500;
//
// and the motion vectors to:
//
//   left_inc_      = 0.010;
//   right_inc_     = 0.015;
//   top_inc_       = 0.005;
//   bottom_inc_    = 0.010;
//
// This example will start the region of interest at the upper left corner, and bounce it around in
// approximately the same manner as the 1st example, but will also gradually expand the window size
// until it is too large, and then steadily shrink it until it is too small. This cycle continues
// forever.
class MovingWindow {
 public:
  MovingWindow();
  ~MovingWindow();
  fuchsia::math::RectF NextWindow();

  struct Window {
    float left;
    float right;
    float top;
    float bottom;
  };

 private:
  fuchsia::math::RectF WindowToRectF(Window window);

  // Test to see if width/height is shrinking/expanding.
  inline bool IsShrinkingWidth() { return left_inc_ > right_inc_; }
  inline bool IsShrinkingHeight() { return top_inc_ > bottom_inc_; }
  inline bool IsExpandingWidth() { return left_inc_ < right_inc_; }
  inline bool IsExpandingHeight() { return top_inc_ < bottom_inc_; }

  float AddToMagnitude(float a, float b);
  void SwapMagnitudes(float* a, float* b);

  // Current window position
  Window curr_window_;

  // Current direction
  float left_inc_;
  float right_inc_;
  float top_inc_;
  float bottom_inc_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_CAMERA_GYM_MOVING_WINDOW_H_
