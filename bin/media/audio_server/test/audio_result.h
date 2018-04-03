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
  static constexpr double kLevelToleranceSource8 = 0.067219077;
  static constexpr double kLevelToleranceOutput8 = 0.065638245;
  static constexpr double kLevelToleranceMix8 = kLevelToleranceSource8;

  static constexpr double kLevelToleranceSource16 = 0.00000068542196;
  static constexpr double kLevelToleranceOutput16 = 0.00000068541681;
  static constexpr double kLevelToleranceInterp16 = 0.00000080781106;
  static constexpr double kLevelToleranceMix16 = 0.00017031199;

  static constexpr double kLevelToleranceSourceFloat = 0.00000099668031;
  static constexpr double kLevelToleranceOutputFloat = 0.00000068541681;
  static constexpr double kLevelToleranceMixFloat = 0.00017069356;
  //
  // Purely when calculating gain (in dB) from gain_scale (fixed-point int),
  // derived values must be within this multiplier (above or below) of target.
  static constexpr double kGainToleranceMultiplier = 1.000001;

  //
  // What is our best-case noise floor in absence of rechannel/gain/SRC/mix.
  // Val is root-sum-square of all other freqs besides the 1kHz reference, in
  // dBr units (compared to magnitude of received reference). Using dBr (not
  // dBFS) includes level attenuation, making this metric a good proxy of
  // frequency-independent fidelity in our audio processing pipeline.
  static double FloorSource8;
  static double FloorMix8;
  static double FloorOutput8;

  static double FloorSource16;
  static double FloorMix16;
  static double FloorOutput16;

  static double FloorSourceFloat;
  static double FloorMixFloat;
  static double FloorOutputFloat;

  static double FloorStereoMono;

  // Val-being-checked (in dBr to reference signal) must be >= this value.
  static constexpr double kPrevFloorSource8 = 49.952957;
  static constexpr double kPrevFloorMix8 = 49.952957;
  static constexpr double kPrevFloorOutput8 = 45.920261;

  static constexpr double kPrevFloorSource16 = 98.104753;
  static constexpr double kPrevFloorMix16 = 90.677331;
  static constexpr double kPrevFloorOutput16 = 98.104753;

  // Note: our internal representation is still int16_t (w/ headroom); this
  // overshadows any precision gain from ingesting/emitting data in float.
  // Until our accumulator is high-precision, float/int16 metrics will be equal.
  static constexpr double kPrevFloorSourceFloat = 98.104911;
  static constexpr double kPrevFloorMixFloat = 91.484408;
  static constexpr double kPrevFloorOutputFloat = 98.104753;

  static double LevelMix8;
  static double LevelMix16;
  static double LevelMixFloat;

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
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespPointDown;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearDown;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespLinearUp;

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
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointUnity;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointDown;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearDown;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearUp;

  //
  // Distortion is measured at a single reference frequency (kReferenceFreq).
  // Sinad (signal-to-noise-and-distortion) is the ratio (in dBr) of reference
  // signal (nominally 1kHz) to the combined power of all OTHER frequencies.
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointDown;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearDown;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearUp;

  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointUnity;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointDown;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearDown;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearUp;

  //
  // Dynamic Range (gain integrity and system response at low volume levels) is
  // measured at a single reference frequency (kReferenceFreq), on a lone mono
  // source without SRC. By determining the smallest possible change in gain
  // that causes a detectable change in output (our 'gain epsilon'), we
  // determine a system's sensitivity to gain changes. We measure not only the
  // output level of the signal, but also the noise level across all other
  // frequencies. Performing these same measurements (output level and noise
  // level) with a gain of -60 dB as well is the standard definition of Dynamic
  // Range testing: by adding 60 dB to the measured signal-to-noise, one
  // determines a system's usable range of data values (translatable into the
  // more accessible Effective Number Of Bits metric). The level measurement at
  // -60 dB is useful not only as a component of the "noise in the presence of
  // signal" calculation, but also as a second avenue toward measuring a
  // system's linearity/accuracy/precision with regard to data scaling and gain.

  // The nearest-unity scale at which we observe effects on signals.
  static constexpr uint32_t kScaleEpsilon = 0x0FFFEFFF;
  // The lowest scale at which full-scale signals are not reduced to zero.
  static constexpr uint32_t kMinScaleNonZero = 0x00001000;

  // Level and unwanted artifacts, applying the smallest-detectable gain change.
  static double LevelEpsilonDown;
  static double SinadEpsilonDown;

  // Level and unwanted artifacts, applying -60dB gain (measures dynamic range).
  static double Level60Down;
  static double Sinad60Down;

  static constexpr double kPrevLevelEpsilonDown = -0.00016807164;
  static constexpr double kPrevDynRangeTolerance = 0.0075380325;

  static constexpr double kPrevSinadEpsilonDown = 93.232593;
  static constexpr double kPrevSinad60Down = 34.196374;

  static constexpr double kPrevLevelStereoMono = -3.01029927;
  static constexpr double kPrevStereoMonoTolerance = 0.00015;
  static constexpr double kPrevFloorStereoMono = 95.958321;

  // class is static only - prevent attempts to instantiate it
  AudioResult() = delete;
};

}  // namespace test
}  // namespace audio
}  // namespace media
