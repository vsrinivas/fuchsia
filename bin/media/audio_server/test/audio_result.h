// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cmath>

namespace media {
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

  // class is static only - prevent attempts to instantiate it
  AudioResult() = delete;
  ~AudioResult() = delete;
};

}  // namespace test
}  // namespace media
