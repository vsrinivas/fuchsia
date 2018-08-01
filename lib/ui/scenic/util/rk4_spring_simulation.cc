// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/util/rk4_spring_simulation.h"

#include <fbl/algorithm.h>
#include <algorithm>
#include <cmath>

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace scenic {
RK4SpringSimulation::RK4SpringSimulation(float initial_value, float tension,
                                         float friction)
    : tension_(tension),
      friction_(friction),
      start_value_(initial_value),
      target_value_(initial_value),
      velocity_(0.0),
      acceleration_multiplier_(0.0),
      is_done_(true) {}

void RK4SpringSimulation::SetTargetValue(float target_value) {
  if (target_value_ != target_value) {
    // If we're flipping the spring we need to flip its velocity.
    bool was_going_positively = target_value_ > start_value_;
    float value = GetValue();
    bool will_be_going_positively = target_value > value;
    if (was_going_positively != will_be_going_positively) {
      velocity_ = -velocity_;
    }
    start_value_ = value;
    target_value_ = target_value;
    if (start_value_ != target_value_) {
      spring_value_ = 0.f;
      is_done_ = false;
      acceleration_multiplier_ = 0.f;
    }
  }
}

float RK4SpringSimulation::GetValue() {
  return fbl::clamp(
      start_value_ + spring_value_ * (target_value_ - start_value_),
      std::min(start_value_, target_value_),
      std::max(start_value_, target_value_));
}

void RK4SpringSimulation::ElapseTime(float seconds) {
  FXL_DCHECK(seconds >= 0);
  if (is_done_)
    return;
  float seconds_remaining = seconds;
  while (seconds_remaining > 0.f) {
    float step_size = std::min(seconds_remaining, kMaxStepSize);
    acceleration_multiplier_ =
        std::min(1.f, acceleration_multiplier_ + step_size * 6.f);
    if (EvaluateRK(step_size)) {
      spring_value_ = 1.f;
      velocity_ = 0.f;
      is_done_ = true;
      acceleration_multiplier_ = 0.f;
      break;
    }
    seconds_remaining -= kMaxStepSize;
  }
}

bool RK4SpringSimulation::EvaluateRK(float step_size) {
  FXL_DCHECK(step_size <= kMaxStepSize);
  float x = spring_value_ - 1.0;
  float v = velocity_;

  float a_dx = v;
  float a_dv = x_acceleration(x, v);

  float b_dx = v + a_dv * (step_size * 0.5);
  float b_dv = x_acceleration(x + a_dx * (step_size * 0.5), b_dx);

  float c_dx = v + b_dv * (step_size * 0.5);
  float c_dv = x_acceleration(x + b_dx * (step_size * 0.5), c_dx);

  float d_dx = v + c_dv * step_size;
  float d_dv = x_acceleration(x + c_dx * step_size, d_dx);

  float dxdt = 1.0 / 6.0 * (a_dx + 2.0 * (b_dx + c_dx) + d_dx);
  float dvdt = 1.0 / 6.0 * (a_dv + 2.0 * (b_dv + c_dv) + d_dv);
  float aft_x = x + dxdt * step_size;
  float aft_v = v + dvdt * step_size;

  spring_value_ = 1.0 + aft_x;
  float final_velocity = aft_v;
  float net_float = aft_x;
  float net_1d_velocity = aft_v;
  bool net_value_is_low = std::abs(net_float) < kTolerance;
  bool net_velocity_is_low = std::abs(net_1d_velocity) < kTolerance;
  velocity_ = final_velocity;

  // Never turn spring back on.
  return net_value_is_low && net_velocity_is_low;
}

}  // namespace scenic
