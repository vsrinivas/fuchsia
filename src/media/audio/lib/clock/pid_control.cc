// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/pid_control.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <optional>
#include <string>

#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace media::audio::clock {

// Reset the PID controller
void PidControl::Reset() {
  *this = PidControl({
      .proportional_factor = proportional_factor_,
      .integral_factor = integral_factor_,
      .derivative_factor = derivative_factor_,
  });
}

// Set the start time and reset the PID controller
void PidControl::Start(zx::time start_time) {
  Reset();
  tune_time_ = start_time;
}

double PidControl::Read() const { return total_pid_contribution_; }

// Factor in the most current error reading. If at/before the previous time, ignore it.
void PidControl::TuneForError(zx::time time_of_error, double error) {
  if (!tune_time_.has_value()) {
    FX_LOGS(INFO) << "TuneForError before Start: setting tune_time_(" << time_of_error.get()
                  << ") and current_error_(" << error << ")";
    tune_time_ = time_of_error;
    current_error_ = error;
    return;
  }

  if (time_of_error <= *tune_time_) {
    FX_LOGS_FIRST_N(WARNING, 100) << __func__ << " ignored, time (" << time_of_error.get()
                                  << ") should exceed previous update (" << tune_time_->get()
                                  << ")";
    return;
  }

  auto duration = static_cast<double>((time_of_error - *tune_time_).get());
  tune_time_ = time_of_error;

  delta_error_ = (error - current_error_) / duration;
  accum_error_ += (error * duration);
  current_error_ = error;

  // TODO(fxbug.dev/83338): if needed, add low-pass filtering to the Derivative contributions
  prop_contrib_ = current_error_ * proportional_factor_;
  integ_contrib_ = accum_error_ * integral_factor_;
  deriv_contrib_ = delta_error_ * derivative_factor_;
  total_pid_contribution_ = prop_contrib_ + integ_contrib_ + deriv_contrib_;
}

std::ostream& operator<<(std::ostream& out, const PidControl& pid) {
  out << &pid << "; Factors "
      << fxl::StringPrintf("%8.1e|%8.1e|%8.1e", pid.proportional_factor_, pid.integral_factor_,
                           pid.derivative_factor_)
      << "; Errors "
      << fxl::StringPrintf("%8.1e|%8.1e|%8.1e", pid.current_error_, pid.accum_error_,
                           pid.delta_error_)
      << "; Contribs "
      << fxl::StringPrintf("%8.1e|%8.1e|%8.1e=%10.2e", pid.prop_contrib_, pid.integ_contrib_,
                           pid.deriv_contrib_, pid.total_pid_contribution_)
      << "; tune_time ";
  if (pid.tune_time_) {
    out << pid.tune_time_->get();
  } else {
    out << "UNSET";
  }
  return out;
}

}  // namespace media::audio::clock
