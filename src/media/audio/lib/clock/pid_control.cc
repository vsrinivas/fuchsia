// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/pid_control.h"

#include <lib/syslog/cpp/macros.h>

namespace media::audio::clock {

// Reset the PID controller; set the initial result & time
void PidControl::Start(int64_t start_time) {
  tune_time_ = start_time;
  total_pid_contribution_ = 0.0;
  current_error_ = accum_error_ = 0.0;
}

double PidControl::Read() const { return total_pid_contribution_; }

// Factor in the most current error reading
void PidControl::TuneForError(int64_t time, double error) {
  FX_DCHECK(time >= tune_time_) << "Time for result-tuning is earlier than previous result ("
                                << time << " < " << tune_time_ << ")";
  // TODO(fxbug.dev/47778): normalize to 10ns units rather than 1ns, if accum_error_ becomes so
  // large that lost precision impacts accuracy (as a double, accum_error_ has 54 bits of
  // precision).
  auto duration = time - tune_time_;
  tune_time_ = time;

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
