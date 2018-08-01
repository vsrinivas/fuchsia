// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_UTIL_RK4_SPRING_SIMULATION_H_
#define GARNET_LIB_UI_SCENIC_UTIL_RK4_SPRING_SIMULATION_H_

namespace scenic {

class RK4SpringSimulation {
 public:
  // |initial_value| is the starting point for the value that's being animated
  // (using the spring-like physics).
  // |tension| and |friction| are parameters for the spring simulation.
  RK4SpringSimulation(float initial_value, float tension = 300.0,
                      float friction = 25.0);

  // The simulation will iteratively approach |target_value|.
  void SetTargetValue(float target_value);

  // Whether the spring simulation has settled.
  bool is_done() { return is_done_; }

  float target_value() { return target_value_; }

  // The current value of the animated value, interpolated from the start and
  // end values using the spring simulation.
  float GetValue();

  // Step forward in time using the spring simulation.
  void ElapseTime(float seconds);

 private:
  float x_acceleration(float x, float velocity) {
    return (-tension_ * x - friction_ * velocity) * acceleration_multiplier_;
  }

  // Evaluates one tick of a spring using the Runge-Kutta Algorithm.
  // This is generally a bit mathy, here is a reasonable intro for those
  // interested:
  //   http://gafferongames.com/game-physics/integration-basics/
  // |step_size| is the duration between steps.
  //   This should be around 1/60th of a second. Values too large will not
  //   work as the spring adjusts itself over time based on previous values,
  //   so if values are larger than 1/60th of a second this method should be
  //   called for each whole 1/60th of a second segment plus the remainder.
  //
  // Returns true if the spring has settled to minimum amount.
  bool EvaluateRK(float step_size);

  // The settle theshold for the spring (for both distance and/or velocity).
  const float kTolerance = 0.01;

  const float kMaxStepSize = 1 / 60.f;

  // The parameters of the simulation.
  const float tension_;
  const float friction_;

  float start_value_;
  float target_value_;

  // The current velocity of the spring.
  float velocity_;

  // The current acceleration multiplier.  This slowly grows as the simulation
  // progresses.  This is used to make the spring more gentle by slowing its
  // initial progress.
  float acceleration_multiplier_;

  bool is_done_;

  /// How far along the spring the current value is, 0 being start and 1 being
  /// end. Because this is a spring that value will go beyond 1 and then back
  /// below 1 etc, as it 'springs'.
  float spring_value_;
};

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_UTIL_RK4_SPRING_SIMULATION_H_
