// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_PID_CONTROL_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_PID_CONTROL_H_

#include <lib/zx/time.h>

#include <optional>
#include <string>

#include <gtest/gtest_prod.h>

namespace media::audio::clock {

// PidControl implements a PID (proportional-integral-derivative) feedback control based on a set of
// coefficients and a sequence of TuneForError calls that inform PidControl of measured errors at
// certain points in time.
class PidControl {
 public:
  FRIEND_TEST(PidControlTest, DefaultAndReset);

  struct Coefficients {
    double proportional_factor = 0;
    double integral_factor = 0;
    double derivative_factor = 0;
  };

  explicit PidControl(const Coefficients& vals)
      : proportional_factor_(vals.proportional_factor),
        integral_factor_(vals.integral_factor),
        derivative_factor_(vals.derivative_factor) {}
  PidControl() = default;

  void Start(zx::time start_time);
  // Calling TuneForError before Start effectively starts (but does not tune) the PidControl.
  // Only tune_time_ and current_error_ are set, and no feedback response is generated.
  void TuneForError(zx::time time, double error);
  // Calling Read on an unstarted or untuned PidControl always returns 0 (no feedback).
  double Read() const;

  friend std::ostream& operator<<(std::ostream& out, const PidControl& pid);

 private:
  void Reset();

  double proportional_factor_ = 0.0;
  double integral_factor_ = 0.0;
  double derivative_factor_ = 0.0;

  double prop_contrib_ = 0.0;
  double integ_contrib_ = 0.0;
  double deriv_contrib_ = 0.0;
  double total_pid_contribution_ = 0.0;

  std::optional<zx::time> tune_time_ = std::nullopt;
  double current_error_ = 0.0;
  double accum_error_ = 0.0;
  double delta_error_ = 0.0;
};

}  // namespace media::audio::clock

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_PID_CONTROL_H_
