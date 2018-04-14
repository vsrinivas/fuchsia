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
// limits (Noise Floor, FrequencyResponse, SignalToNoiseAndDistortion), values
// being assessed should be **greater than or equal to** the specified limit.
//
// We save previous results to 8-digit accuracy (>23 bits), exceeding float32
// precision. This does not pose a risk of 'flaky test' since the math should
// be the same every time. With no real dependencies outside FBL, we expect
// any change that affects these results to be directly within the core
// objects (Mixer, Gain, OutputFormatter), and the corresponding adjustments
// to these thresholds should be included with that CL.
//
// Measurements and thresholds grouped into stages (where our pipeline is
// represented by the 6 stages Input|Rechannel|Interpolate|Scale|Sum|Output).
class AudioResult {
 public:
  //
  // Input
  //
  // How close is a measured level to the reference dB level?  Val-being-checked
  // must be within this distance (above OR below) from the reference dB level.
  static constexpr double kLevelToleranceSource8 = 0.067219077;
  static constexpr double kLevelToleranceSource16 = 0.00000068542196;
  static constexpr double kLevelToleranceSourceFloat = 0.00000099668031;

  // What is our best-case noise floor in absence of rechannel/gain/SRC/mix.
  // Val is root-sum-square of all other freqs besides the 1kHz reference, in
  // dBr units (compared to magnitude of received reference). Using dBr (not
  // dBFS) includes level attenuation, making this metric a good proxy of
  // frequency-independent fidelity in our audio processing pipeline.
  static double FloorSource8;
  static double FloorSource16;
  static double FloorSourceFloat;

  // Val-being-checked (in dBr to reference signal) must be >= this value.
  static constexpr double kPrevFloorSource8 = 49.952957;
  static constexpr double kPrevFloorSource16 = 98.104753;
  static constexpr double kPrevFloorSourceFloat = 98.104911;
  // Note: our internal representation is still int16_t (w/ headroom); this
  // overshadows any precision gain from ingesting/emitting data in float.
  // Until our accumulator is high-precision, float/int16 metrics will be equal.

  //
  // Rechannel
  //
  // Previously-cached thresholds related to stereo-to-mono mixing.
  static constexpr double kPrevStereoMonoTolerance = 0.00015;
  static constexpr double kPrevLevelStereoMono = -3.01029927;

  static double FloorStereoMono;
  static constexpr double kPrevFloorStereoMono = 95.958321;

  //
  // Interpolate
  //
  // Compared to 1:1 accuracy (kLevelToleranceSourceFloat), LinearSampler boosts
  // low-frequencies during any significant up-sampling (e.g. 1:2). This
  // tolerance represents how far above 0.0 dB is acceptable.
  static constexpr double kLevelToleranceInterpolation = 0.000060906023;
  //            Prior to change in resampler tests, was: 0.000038069078

  // What is our received level (in dBFS), when sending sinusoids through our
  // mixers at certain resampling ratios. PointSampler and LinearSampler are
  // specifically targeted with resampling ratios that represent how the current
  // system uses them. A more exhaustive set is available for in-depth testing
  // outside of CQ (--full switch). We test PointSampler at 1:1 (no SRC) and 2:1
  // (96k-to-48k), and LinearSampler at 294:160 and 147:160 (e.g. 88.2k-to-48k
  // and 44.1k-to-48k). Additional ratios are available with the --full switch.
  // Our entire set of ratios is present in the below arrays: Unity (1:1), Down1
  // (2:1), Down2 (294:160), Up1 (147:160) and Up2 (1:2).
  //
  // We perform frequency response tests at various frequencies (kSummaryFreqs[]
  // from frequency_set.h), storing the result at each frequency. As with
  // resampling ratios, subsequent CL contains a more exhaustive frequency set,
  // for in-depth testing and diagnostics to be done outside CQ.
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespPointUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespPointUp2;

  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespLinearUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespLinearUp2;

  // The values cached below are less than or equal to 0 dBFS. The value being
  // checked must be greater than or equal to this value (but can be no greater
  // than kLevelToleranceInterpolation, as 0 dBFS is the target level).
  //
  // With rates other than N:1, interpolating resamplers dampen high frequencies
  // -- as shown in the previous-result threshold values in audio_result.cc.
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointUnity;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointDown1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointDown2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointUp1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointUp2;

  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearUnity;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearDown1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearDown2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearUp1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearUp2;

  //
  // Signal-to-Noise-And-Distortion (SINAD)
  //
  // Sinad (signal-to-noise-and-distortion) is the ratio (in dBr) of reference
  // signal as received (nominally from a 1kHz input), compared to the power of
  // all OTHER frequencies (combined via root-sum-square).
  //
  // Distortion is often measured at one reference frequency (kReferenceFreq).
  // We measure noise floor at only 1 kHz, and for summary SINAD tests use 40
  // Hz, 1 kHz and 12 kHz. For full-spectrum tests we test 47 frequencies.
  // These arrays hold various SINAD results as measured during the test run.
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointUp2;

  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearUp2;

  // For SINAD, measured value must exceed or equal the below cached value.
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointUnity;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointDown1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointDown2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointUp1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointUp2;

  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearUnity;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearDown1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearDown2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearUp1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearUp2;

  //
  // Scale
  //
  // Purely when calculating gain (in dB) from gain_scale (fixed-point int),
  // derived values must be within this multiplier (above or below) of target.
  static constexpr double kGainToleranceMultiplier = 1.000001;

  // The nearest-unity scale at which we observe effects on signals.
  static constexpr uint32_t kScaleEpsilon = 0x0FFFEFFF;
  // The lowest scale at which full-scale signals are not reduced to zero.
  static constexpr uint32_t kMinScaleNonZero = 0x00001000;

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

  // For dynamic range, level-being-checked (in dBFS) should be within
  // kPrevDynRangeTolerance of the dB gain setting (e.g. -60.0 dB).
  static constexpr double kPrevDynRangeTolerance = 0.0075380325;

  // Level and unwanted artifacts -- as well as previously-cached threshold
  // limits for the same -- when applying the smallest-detectable gain change.
  static double LevelEpsilonDown;
  static constexpr double kPrevLevelEpsilonDown = -0.00016807164;

  static double SinadEpsilonDown;
  static constexpr double kPrevSinadEpsilonDown = 93.232593;

  // Level and unwanted artifacts -- as well as previously-cached threshold
  // limits for the same -- when applying -60dB gain (measures dynamic range).
  static double Level60Down;

  static double Sinad60Down;
  static constexpr double kPrevSinad60Down = 34.196374;

  //
  // Sum
  //
  // How close is a measured level to the reference dB level?  Val-being-checked
  // must be within this distance (above OR below) from the reference dB level.
  static constexpr double kLevelToleranceMix8 = 0.067219077;
  static constexpr double kLevelToleranceMix16 = 0.00017031199;
  static constexpr double kLevelToleranceMixFloat = 0.00017069356;

  static double LevelMix8;
  static double LevelMix16;
  static double LevelMixFloat;

  static double FloorMix8;
  static double FloorMix16;
  static double FloorMixFloat;

  static constexpr double kPrevFloorMix8 = 49.952957;
  static constexpr double kPrevFloorMix16 = 90.677331;
  static constexpr double kPrevFloorMixFloat = 91.484408;

  //
  // Output
  //
  // How close is a measured level to the reference dB level?  Val-being-checked
  // must be within this distance (above OR below) from the reference dB level.
  static constexpr double kLevelToleranceOutput8 = 0.065638245;
  static constexpr double kLevelToleranceOutput16 = 0.00000068541681;
  static constexpr double kLevelToleranceOutputFloat = 0.00000068541681;

  // What is our best-case noise floor in absence of rechannel/gain/SRC/mix.
  // Val is root-sum-square of all other freqs besides the 1kHz reference, in
  // dBr units (compared to magnitude of received reference). Using dBr (not
  // dBFS) includes level attenuation, making this metric a good proxy of
  // frequency-independent fidelity in our audio processing pipeline.
  static double FloorOutput8;
  static double FloorOutput16;
  static double FloorOutputFloat;

  static constexpr double kPrevFloorOutput8 = 45.920261;
  static constexpr double kPrevFloorOutput16 = 98.104753;
  static constexpr double kPrevFloorOutputFloat = 98.104753;

  // class is static only - prevent attempts to instantiate it
  AudioResult() = delete;
};

}  // namespace test
}  // namespace audio
}  // namespace media

/*
    AudioResult journal - updated upon each CL that affects these measurements

    2018-04-24  Converted fidelity tests to float-based input, instead of 16-bit
                signed integers -- enabling higher-resolution measurement (and
                requiring updates to most thresholds).
    2018-04-23  Moved fidelity tests to call Mixer objects in smaller mix jobs,
                to emulate how these objects are used by their callers elsewhere
                in Audio_Server. By forcing source-to-accumulator buffer lengths
                to match the required ratios, we directly expose a longstanding
                source of distortion, MTWN-49 (the "step_size" bug).
    2018-03-28  Full-spectrum frequency response and distortion tests: in all,
                47 frequencies, from DC, 13Hz, 20Hz to 22kHz, 24kHz and beyond.
                Down-sampling tests show significant aliasing.
    2018-03-28  Initial mix floor tests: 8- and 16-bit for accumulation.
    2018-03-26  Initial dynamic range tests. kScaleEpsilon = 0FFFFFFF for
                incoming positive values; 0FFFE000 for negative values.
    2018-03-21  Initial frequency response / sinad tests: 1kHz, 40Hz, 12kHz.
    2018-03-20  Initial source/output noise floor tests: 8- & 16-bit, 1kHz.

*/
