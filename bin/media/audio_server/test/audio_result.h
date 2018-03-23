// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cmath>
#include "garnet/bin/media/audio_server/test/frequency_set.h"

namespace media {
namespace audio {
namespace test {

// Audio measurements that are determined by various test cases throughout the
// overall set. These measurements are eventually displayed in an overall recap,
// after all other tests have completed.
//
// Although these audio measurements are quantitative, there is no 'right
// answer' per se. Rather, we compare current measurements to those previously
// measured, to detect any fidelity regressions. Because the code being tested
// is largely mathematical (only dependencies being a few FBL functions), we
// will fail on ANY regression, since presumably an intentional change in our
// fidelity would contain in that same CL a change to these thresholds.
//
// All reference values and measured values are in decibels (+20dB => 10x magn).
// When comparing values to the below limits, a specified 'tolerance' refers to
// the maximum delta (positive OR negative) from reference value.  For ALL OTHER
// limits (Noise Floor, FrequencyResponse, SignalToNoiseAndDistortion), a value
// being assessed should simply be greater than or equal to the specified limit.
class AudioResult {
 public:
  //
  // How close is a measured level to the reference dB level?  Val-being-checked
  // must be within this distance (above OR below) from the reference dB level.
  static constexpr double kLevelToleranceSource8 = 0.1;
  static constexpr double kLevelToleranceOutput8 = 0.1;
  static constexpr double kLevelToleranceSource16 = 0.000001;
  static constexpr double kLevelToleranceOutput16 = 0.000001;

  //
  // What is our best-case noise floor in absence of rechannel/gain/SRC/mix.
  // Val is root-sum-square of all other freqs besides the 1kHz reference, in
  // dBr units (compared to magnitude of received reference). Using dBr (not
  // dBFS) includes level attenuation, making this metric a good proxy of
  // frequency-independent fidelity in our audio processing pipeline.
  static double FloorSource8;
  static double FloorOutput8;
  static double FloorSource16;
  static double FloorOutput16;

  // Val-being-checked (in dBr to reference signal) must be >= this value.
  static constexpr double kPrevFloorSource8 = 49.952957;
  static constexpr double kPrevFloorOutput8 = 45.920261;
  static constexpr double kPrevFloorSource16 = 98.104753;
  static constexpr double kPrevFloorOutput16 = 98.104753;

  //
  // What is our received level (in dBFS), when sending sinusoids through our
  // mixers at certain resampling ratios. The PointSampler and LinearSampler
  // objects are specifically targeted with resampling ratios that are highly
  // representative of how the current system uses them. A more exhaustive set
  // of ratios will be included in subsequent CL, for more in-depth testing
  // outside of CQ.
  //
  // We test PointSampler at Unity (no SRC) and 2:1 (such as 96-to-48), and
  // LinearSampler at 294:160 and 147:160 (e.g. 88.2-to-48 and 44.1-to-48).
  //
  // We perform frequency fesponse tests at various frequencies (kSummaryFreqs[]
  // from frequency_set.h), storing the result at each frequency. As with
  // resampling ratios, subsequent CL contains a more exhaustive frequency set,
  // for in-depth testing and diagnostics to be done outside CQ.
  static double FreqRespPointUnity[FrequencySet::kNumSummaryFreqs];
  static double FreqRespPointDown[FrequencySet::kNumSummaryFreqs];
  static double FreqRespLinearDown[FrequencySet::kNumSummaryFreqs];
  static double FreqRespLinearUp[FrequencySet::kNumSummaryFreqs];

  // Val-being-checked (in dBFS) must be greater than or equal to this value.
  // For these 1:1 and N:1 ratios, PointSampler's frequency response is ideal
  // (flat). It is actually very slightly positive; for completeness we verify
  // that response is no greater than (0.0 + kLevelToleranceSource16).
  //
  // Note: with rates other than N:1 or 1:N, interpolating resamplers dampen
  // high frequencies -- as shown in previously-saved LinearSampler results.
  //
  // We save previous results to 8-digit accuracy (>23 bits), exceeding float32
  // precision. This does not pose a risk of 'flaky test' since the math should
  // be the same every time. With no real dependencies outside FBL, we expect
  // any change that affects these results to be directly within the core
  // objects (Mixer, Gain, OutputFormatter), and the corresponding adjustments
  // to these thresholds should be included with that CL.
  //
  // clang-format off
  static constexpr double kPrevFreqRespPointUnity[] =
      { 0.0,               0.0,            0.0        };
  static constexpr double kPrevFreqRespPointDown[]  =
      { 0.0,               0.0,            0.0        };
  static constexpr double kPrevFreqRespLinearDown[] =
      { -0.0000094623437, -0.0036589951,  -0.53193138 };
  static constexpr double kPrevFreqRespLinearUp[]   =
      { -0.000026934650,  -0.014647070,   -2.1693107  };
  // clang-format on

  //
  // Distortion is measured at a single reference frequency (kReferenceFreq).
  // Sinad (signal-to-noise-and-distortion) is the ratio (in dBr) of reference
  // signal (nominally 1kHz) to the combined power of all OTHER frequencies.
  static double SinadPointUnity;
  static double SinadPointDown;
  static double SinadLinearDown;
  static double SinadLinearUp;

  static constexpr double kPrevSinadPointUnity = 98.104753;
  static constexpr double kPrevSinadPointDown = 98.104753;
  static constexpr double kPrevSinadLinearDown = 74.421795;
  static constexpr double kPrevSinadLinearUp = 62.428449;

  // class is static only - prevent attempts to instantiate it
  AudioResult() = delete;
};

}  // namespace test
}  // namespace audio
}  // namespace media
