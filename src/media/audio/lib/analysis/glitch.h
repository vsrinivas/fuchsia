// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_ANALYSIS_GLITCH_H_
#define SRC_MEDIA_AUDIO_LIB_ANALYSIS_GLITCH_H_

#include <zircon/types.h>

#include <cmath>
#include <limits>

#include "src/media/audio/lib/format/audio_buffer.h"

namespace media::audio {

// A utility class that can be used to detect audio discontinuities ("glitches").
//
// SlopeChecker verifies a one-channel stream containing a full-scale, sine wave signal at known
// frequency. If the slope exceeds the expected maximum for that frequency, it returns false.
class SlopeChecker {
 public:
  SlopeChecker(int32_t samples_per_second, int32_t expected_frequency,
               double expected_max_amplitude = 1.0, const std::string& tag = "")
      : expected_max_amplitude_(expected_max_amplitude), tag_(tag.empty() ? tag + ": " : tag) {
    double samples_per_period =
        static_cast<double>(samples_per_second) / static_cast<double>(expected_frequency);
    // Max delta for a sine of this freq is the diff between vals at -1/2 smpls and +1/2 smpls.
    // Val at +1/2 smpls is sin(2 * pi * 1/2 / samples_per_period) * max_ampl, which equals:
    //   sin(pi/samples_per_period) * max_ampl.
    // This is the change in Y, across X-axis span [0, 1/2]. Sinusoids are symmetric across the
    // origin, so we multiply by 2.0 to get the change in Y-axis, across X-axis span [-1/2, +1/2].
    max_expected_slope_ = std::sin(M_PI / samples_per_period) * expected_max_amplitude_ * 2.0;
  }

  bool Check(float sample, bool print = false) {
    bool ret = true;
    auto diff = sample - *prev_sample_;
    if (std::abs(sample) > expected_max_amplitude_ ||
        (prev_sample_ &&
         std::abs(diff) > max_expected_slope_ + std::numeric_limits<float>::epsilon())) {
      if (print) {
        FX_LOGS(ERROR) << tag_ << "********** slope discontinuity detected. diff " << diff
                       << "; max_expected " << max_expected_slope_ << " (prev " << *prev_sample_
                       << ", new " << sample << ")";
      }
      ret = false;
    }
    prev_sample_ = sample;
    return ret;
  }

  void Reset() { prev_sample_ = std::nullopt; }

 private:
  const double expected_max_amplitude_;
  const std::string tag_;

  double max_expected_slope_;
  std::optional<float> prev_sample_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_ANALYSIS_GLITCH_H_
