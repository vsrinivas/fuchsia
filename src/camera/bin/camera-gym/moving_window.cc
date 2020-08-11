// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/camera-gym/moving_window.h"

#include <fuchsia/math/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/types.h>

namespace camera {

constexpr float kMargin = 0.00001;
constexpr float kMinimumWidth = 0.100;
constexpr float kMinimumHeight = 0.100;

MovingWindow::MovingWindow() {
  // Initialize position.
  curr_window_.left = 0.000;
  curr_window_.right = 0.400;
  curr_window_.top = 0.000;
  curr_window_.bottom = 0.400;

  // Initialize motion vectors.
  left_inc_ = 0.0050;
  right_inc_ = 0.0050;
  top_inc_ = 0.0025;
  bottom_inc_ = 0.0025;
}

MovingWindow::~MovingWindow() {}

fuchsia::math::RectF MovingWindow::NextWindow() {
  // Sanity check basic assumption about window position and direction.
  assert(curr_window_.right >= curr_window_.left);
  assert(curr_window_.bottom >= curr_window_.top);
  assert(((left_inc_ < 0.0) && (right_inc_ < 0.0)) || ((left_inc_ > 0.0) && (right_inc_ > 0.0)));
  assert(((top_inc_ < 0.0) && (bottom_inc_ < 0.0)) || ((top_inc_ > 0.0) && (bottom_inc_ > 0.0)));

  // Calculate next window position using current direction.
  Window next_window;
  next_window.left = curr_window_.left + left_inc_;
  next_window.right = curr_window_.right + right_inc_;
  next_window.top = curr_window_.top + top_inc_;
  next_window.bottom = curr_window_.bottom + bottom_inc_;

  // Current direction should not cause new window position to violate assumptions.
  assert(next_window.right >= next_window.left);
  assert(next_window.bottom >= next_window.top);

  float next_width = next_window.right - next_window.left;
  float next_height = next_window.bottom - next_window.top;

  assert(next_width > 0.0);
  assert(next_height > 0.0);

  bool hit_left = (next_window.left <= (0.0 + kMargin)) && (left_inc_ < 0.0);
  bool hit_right = (next_window.right >= (1.0 - kMargin)) && (right_inc_ > 0.0);
  bool hit_top = (next_window.top <= (0.0 + kMargin)) && (top_inc_ < 0.0);
  bool hit_bottom = (next_window.bottom >= (1.0 - kMargin)) && (bottom_inc_ > 0.0);

  bool hit_minimum_width = IsShrinkingWidth() && (next_width <= kMinimumWidth + kMargin);
  bool hit_minimum_height = IsShrinkingHeight() && (next_height <= kMinimumHeight + kMargin);

  if (hit_minimum_width || hit_minimum_height) {
    // Start expanding width & height
    right_inc_ = AddToMagnitude(left_inc_, 0.010);
    bottom_inc_ = AddToMagnitude(top_inc_, 0.005);
  }

  bool hit_left_right = hit_left && hit_right;
  bool hit_top_bottom = hit_top && hit_bottom;

  bool hit_opposite_sides = hit_left_right || hit_top_bottom;

  // Count up how many sides were hit.
  uint32_t hit_count =
      (hit_left ? 1 : 0) + (hit_right ? 1 : 0) + (hit_top ? 1 : 0) + (hit_bottom ? 1 : 0);

  // If we hit 2 opposite sides, start shrinking.
  if (hit_opposite_sides) {
    // Start shrinking width & height
    right_inc_ = AddToMagnitude(left_inc_, -0.010);
    bottom_inc_ = AddToMagnitude(top_inc_, -0.005);
    curr_window_ = std::move(next_window);
    return WindowToRectF(curr_window_);
  }

  // If window hit a corner, reverse X/Y directions and swap increments.
  if (hit_count == 2) {
    // Swap X & Y increment magnitudes.
    SwapMagnitudes(&left_inc_, &top_inc_);
    SwapMagnitudes(&right_inc_, &bottom_inc_);

    // Reverse X direction.
    left_inc_ = -left_inc_;
    right_inc_ = -right_inc_;

    // Reverse Y direction.
    top_inc_ = -top_inc_;
    bottom_inc_ = -bottom_inc_;

    curr_window_ = std::move(next_window);
    return WindowToRectF(curr_window_);
  }

  // If window hits only left or right side, reverse the X direction.
  if (hit_left || hit_right) {
    left_inc_ = -left_inc_;
    right_inc_ = -right_inc_;
  }

  // If window hits only top or bottom side, reverse the Y direction.
  if (hit_top || hit_bottom) {
    top_inc_ = -top_inc_;
    bottom_inc_ = -bottom_inc_;
  }

  curr_window_ = std::move(next_window);
  return WindowToRectF(curr_window_);
}

fuchsia::math::RectF MovingWindow::WindowToRectF(Window window) {
  // Make sure the window is within legal bounds.
  window.left = std::max(window.left, 0.0f);
  window.right = std::min(window.right, 1.0f);
  window.top = std::max(window.top, 0.0f);
  window.bottom = std::min(window.bottom, 1.0f);

  // Make sure the relative positions are correct.
  assert(window.right >= window.left);
  assert(window.bottom >= window.top);

  fuchsia::math::RectF rect;
  rect.x = window.left;
  rect.y = window.top;
  rect.width = window.right - window.left;
  rect.height = window.bottom - window.top;
  return rect;
}

float MovingWindow::AddToMagnitude(float a, float b) {
  float a_magnitude = std::abs(a);
  float a_sign = (a < 0.0) ? -1.0 : 1.0;

  return a_sign * (a_magnitude + b);
}

void MovingWindow::SwapMagnitudes(float* a, float* b) {
  float a_magnitude = std::abs(*a);
  float a_sign = (*a < 0.0) ? -1.0 : 1.0;

  float b_magnitude = std::abs(*b);
  float b_sign = (*b < 0.0) ? -1.0 : 1.0;

  *a = a_sign * b_magnitude;
  *b = b_sign * a_magnitude;
}

}  // namespace camera
