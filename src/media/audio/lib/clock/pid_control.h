// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_PID_CONTROL_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_PID_CONTROL_H_

#include <zircon/time.h>

#include <array>

namespace media::audio::clock {

// PidControl implements a PID (proportional-integral-derivative) feedback control based on a set of
// coefficients and a sequence of TuneForError calls that inform PidControl of measured errors at
// certain points in time.
//
// Note: "time" must advance monotonically and be independent of the output variable. As long as
// they are used consistently across Start() and TuneForError(), the units for "time" can be any
// 64-bit value (zx_time_t, audio frames, etc).
//
// TODO(fxbug.dev/60531): Once we have another client, templatize PidControl for "time" type.
class PidControl {
  friend class PidControlTest;

 public:
  struct Coefficients {
    double proportional_factor = 0;
    double integral_factor = 0;
    double derivative_factor = 0;
  };

  explicit PidControl(const Coefficients& vals)
      : proportional_factor_(vals.proportional_factor),
        integral_factor_(vals.integral_factor),
        derivative_factor_(vals.derivative_factor) {
    Start(0);
  }
  PidControl() : PidControl(Coefficients{}) {}

  void Start(int64_t start_time);

  double Read() const;
  void TuneForError(int64_t time, double error);

  void DisplayCoefficients();

 private:
  double proportional_factor_;
  double integral_factor_;
  double derivative_factor_;

  double prop_contrib_;
  double integ_contrib_;
  double deriv_contrib_;
  double total_pid_contribution_;

  int64_t tune_time_;
  double current_error_;
  double accum_error_;
  double delta_error_;
};

}  // namespace media::audio::clock

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_PID_CONTROL_H_
