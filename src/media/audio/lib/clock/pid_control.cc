// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/pid_control.h"

#include <lib/syslog/cpp/macros.h>

namespace media::audio::clock {

// Reset the PID controller; set the initial result & time
void PidControl::Start(zx::time start_time) {
  tune_time_ = start_time;
  total_pid_contribution_ = 0.0;
  current_error_ = accum_error_ = 0.0;
}

double PidControl::Read() const { return total_pid_contribution_; }

// Factor in the most current error reading
void PidControl::TuneForError(zx::time time_of_error, double error) {
  // If our previous update was at this time, ignore it.
  // This occurs if Start then TuneForError are called, with the same zx::time.
  if (time_of_error == tune_time_) {
    return;
  }
  FX_CHECK(time_of_error > tune_time_)
      << "Time for tuning (" << time_of_error.get() << ") is earlier than previous update ("
      << tune_time_.get() << ")";

  // TODO(fxbug.dev/47778): normalize from 1ns units to 10ns, if accum_error_ becomes so large that
  // lost precision impacts accuracy (as a double, accum_error_ has 54 bits of precision).
  auto duration = (time_of_error - tune_time_).get();
  tune_time_ = time_of_error;

  delta_error_ = (error - current_error_) / duration;
  accum_error_ += (error * duration);
  current_error_ = error;

  prop_contrib_ = current_error_ * proportional_factor_;
  integ_contrib_ = accum_error_ * integral_factor_;
  deriv_contrib_ = delta_error_ * derivative_factor_;
  total_pid_contribution_ = prop_contrib_ + integ_contrib_ + deriv_contrib_;
}

void PidControl::DisplayCoefficients() {
  FX_LOGS(INFO) << "Factors: P " << proportional_factor_ << ",  I " << integral_factor_ << ",  D "
                << derivative_factor_ << "; Contributions of p " << prop_contrib_ << ",  i "
                << integ_contrib_ << ",  d " << deriv_contrib_ << "; Total Contrib "
                << total_pid_contribution_ << "\n";
}

}  // namespace media::audio::clock
