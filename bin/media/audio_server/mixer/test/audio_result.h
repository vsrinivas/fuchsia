// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_TEST_AUDIO_RESULT_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_TEST_AUDIO_RESULT_H_

#include <cmath>
#include "garnet/bin/media/audio_server/constants.h"
#include "garnet/bin/media/audio_server/gain.h"
#include "garnet/bin/media/audio_server/mixer/test/frequency_set.h"

namespace media {
namespace audio {
namespace test {

// Audio measurements that are determined by various test cases throughout the
// overall set. These measurements are eventually displayed in an overall recap,
// after all other tests have completed.
//
// We perform frequency tests at various frequencies (kSummaryFreqs[] from
// frequency_set.h), storing the result for each frequency.
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
  //
  // Input
  //
  // How close is a measured level to the reference dB level?  Val-being-checked
  // must be within this distance (above OR below) from the reference dB level.
  static double LevelToleranceSource8;
  static double LevelToleranceSource16;
  static double LevelToleranceSourceFloat;

  static constexpr double kPrevLevelToleranceSource8 = 6.7219077e-02;
  static constexpr double kPrevLevelToleranceSource16 = 1.0548786e-06;
  static constexpr double kPrevLevelToleranceSourceFloat = 1.0548786e-06;

  static double LevelSource8;
  static double LevelSource16;
  static double LevelSourceFloat;

  static constexpr double kPrevLevelSource8 = 0.0;
  static constexpr double kPrevLevelSource16 = 0.0;
  static constexpr double kPrevLevelSourceFloat = 0.0;

  // What is our best-case noise floor in absence of rechannel/gain/SRC/mix.
  // Val is root-sum-square of all other freqs besides the 1kHz reference, in
  // dBr units (compared to magnitude of received reference). Using dBr (not
  // dBFS) includes level attenuation, making this metric a good proxy of
  // frequency-independent fidelity in our audio processing pipeline.
  static double FloorSource8;
  static double FloorSource16;
  static double FloorSourceFloat;

  // Val-being-checked (in dBr to reference signal) must be >= these values.
  static constexpr double kPrevFloorSource8 = 49.952957;
  static constexpr double kPrevFloorSource16 = 98.104753;
  static constexpr double kPrevFloorSourceFloat = 98.104911;

  //
  //
  // Rechannel
  //
  // Previously-cached thresholds related to stereo-to-mono mixing.
  static double LevelToleranceStereoMono;
  static constexpr double kPrevLevelToleranceStereoMono = 2.9724227e-05;

  static double LevelStereoMono;
  static constexpr double kPrevLevelStereoMono = -3.01029996;

  static double FloorStereoMono;
  static constexpr double kPrevFloorStereoMono = 93.607405;

  //
  //
  // Interpolate
  //
  // Compared to 1:1 accuracy (kLevelToleranceSourceFloat), LinearSampler boosts
  // low-frequencies during any significant up-sampling (e.g. 1:2).
  // kPrevLevelToleranceInterpolation is how far above 0dB we allow.
  static double LevelToleranceInterpolation;
  static constexpr double kPrevLevelToleranceInterpolation = 1.0933640e-03;

  // Frequency Response
  //
  // What is our received level (in dBFS), when sending sinusoids through our
  // mixer at certain resampling ratios. PointSampler and LinearSampler are
  // specifically targeted with resampling ratios that represent how the current
  // system uses them. A more exhaustive set is available for in-depth testing
  // outside of CQ (--full switch). We test PointSampler at 1:1 (no SRC) and 2:1
  // (96k-to-48k), and LinearSampler at 294:160 and 147:160 (e.g. 88.2k-to-48k
  // and 44.1k-to-48k). Additional ratios are available with the --full switch.
  // Our entire set of ratios is present in the below arrays: Unity (1:1), Down1
  // (2:1), Down2 (294:160), Up1 (147:160) and Up2 (1:2).
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespPointUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespPointUp2;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointMicro;

  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespLinearUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespLinearUp2;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearMicro;

  //
  // Val-being-checked (in dBFS) must be greater than or equal to this value.
  // It also cannot be more than kPrevLevelToleranceInterpolation above 0.0db.
  // For these 1:1 and N:1 ratios, PointSampler's frequency response is ideal
  // (flat). It is actually very slightly positive (hence the tolerance check).
  //
  // Note: with rates other than N:1 or 1:N, interpolating resamplers dampen
  // high frequencies -- as shown in previously-saved LinearSampler results.
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
      kPrevFreqRespPointMicro;

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
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearMicro;

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
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointMicro;

  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearUp2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearMicro;

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
      kPrevSinadPointMicro;

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
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearMicro;

  //
  //
  // Scale
  //
  // The lowest (furthest-from-Unity) AScale with no observable attenuation on
  // full-scale data (i.e. the smallest AScale indistinguishable from Unity).
  //
  // For 24-bit scalar precision, this scalar multiplied by full-scale 1.0
  // should produce 0.FFFFC0, which (in 18-bit pipeline) exactly rounds up to 1.
  // With current precision values, this scalar is (0x1000000-0x40)/0x1000000.
  static constexpr Gain::AScale kMinUnityScale =
      ((1 << (Gain::kFractionalScaleBits)) -
       (1 << (Gain::kFractionalScaleBits - kAudioPipelineWidth))) /
      static_cast<Gain::AScale>(1 << (Gain::kFractionalScaleBits));

  // The highest (closest-to-Unity) AScale with an observable effect on
  // full-scale (i.e. the largest sub-Unity AScale distinguishable from Unity).
  //
  // This const is the smallest discernable decrement below kMinUnityScale.
  // For 18-bit data and float scale, this equals (0x1000000-0x40-1)/0x1000000.
  static constexpr Gain::AScale kPrevScaleEpsilon =
      ((1 << (Gain::kFractionalScaleBits)) -
       (1 << (Gain::kFractionalScaleBits - kAudioPipelineWidth)) - 1) /
      static_cast<Gain::AScale>(1 << (Gain::kFractionalScaleBits));

  // The lowest (closest-to-zero) AScale at which full-scale data are not
  // silenced (i.e. the smallest AScale that is distinguishable from Mute).
  //
  // This scalar mirrors kMinUnityScale above. This scalar multiplied by full-
  // scale should produce 0.000040. For 18-bit pipeline, this exactly rounds up
  // to the last non-zero value. Given our current precision (18-bit data, float
  // scale), this scalar is 0x40/0x1000000.
  static constexpr Gain::AScale kPrevMinScaleNonZero =
      (1 << (Gain::kFractionalScaleBits - kAudioPipelineWidth)) /
      static_cast<Gain::AScale>(1 << (Gain::kFractionalScaleBits));

  // The highest (furthest-from-Mute) AScale at which full-scale data are
  // silenced (i.e. the largest AScale that is indistinguishable from Mute).
  //
  // This is kPrevMinScaleNonZero, minus the smallest discernable decrement.
  // For 18-bit data and float scale, this val is (0x40-1)/0x1000000.
  static constexpr Gain::AScale kMaxScaleZero =
      ((1 << (Gain::kFractionalScaleBits - kAudioPipelineWidth)) - 1) /
      static_cast<Gain::AScale>(1 << (Gain::kFractionalScaleBits));

  static Gain::AScale ScaleEpsilon;
  static Gain::AScale MinScaleNonZero;

  // Dynamic Range
  // (gain integrity and system response at low volume levels)
  //
  // Measured at a single reference frequency (kReferenceFreq), on a lone mono
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
  static double DynRangeTolerance;
  static constexpr double kPrevDynRangeTolerance = 7.5380325e-03;

  // Level and unwanted artifacts, applying the smallest-detectable gain change.
  static double LevelEpsilonDown;
  static constexpr double kPrevLevelEpsilonDown = -1.6807164e-04;

  static double SinadEpsilonDown;
  static constexpr double kPrevSinadEpsilonDown = 93.232593;

  // Level and unwanted artifacts -- as well as previously-cached threshold
  // limits for the same -- when applying -60dB gain (measures dynamic range).
  static double Level60Down;
  static constexpr double kPrevLevel60Down = 60.0;

  static double Sinad60Down;
  static constexpr double kPrevSinad60Down = 34.196374;

  //
  //
  // Sum
  //
  // How close is a measured level to the reference dB level?  Val-being-checked
  // must be within this distance (above OR below) from the reference dB level.
  static double LevelToleranceMix8;
  static double LevelToleranceMix16;
  static double LevelToleranceMixFloat;

  static constexpr double kPrevLevelToleranceMix8 = 6.7219077e-02;
  static constexpr double kPrevLevelToleranceMix16 = 1.7031199e-04;
  static constexpr double kPrevLevelToleranceMixFloat = 1.7069356e-04;

  static double LevelMix8;
  static double LevelMix16;
  static double LevelMixFloat;

  static constexpr double kPrevLevelMix8 = 0.0;
  static constexpr double kPrevLevelMix16 = 0.0;
  static constexpr double kPrevLevelMixFloat = 0.0;

  static double FloorMix8;
  static double FloorMix16;
  static double FloorMixFloat;

  static constexpr double kPrevFloorMix8 = 49.952317;
  static constexpr double kPrevFloorMix16 = 90.677331;
  static constexpr double kPrevFloorMixFloat = 91.484408;

  //
  //
  // Output
  //
  // How close is a measured level to the reference dB level?  Val-being-checked
  // must be within this distance (above OR below) from the reference dB level.
  static double LevelToleranceOutput8;
  static double LevelToleranceOutput16;
  static double LevelToleranceOutputFloat;

  static constexpr double kPrevLevelToleranceOutput8 = 6.5638245e-02;
  static constexpr double kPrevLevelToleranceOutput16 = 8.4876728e-05;
  static constexpr double kPrevLevelToleranceOutputFloat = 6.8541681e-07;

  static double LevelOutput8;
  static double LevelOutput16;
  static double LevelOutputFloat;

  static constexpr double kPrevLevelOutput8 = 0.0;
  static constexpr double kPrevLevelOutput16 = 0.0;
  static constexpr double kPrevLevelOutputFloat = 0.0;

  // What is our best-case noise floor in absence of rechannel/gain/SRC/mix.
  // Val is root-sum-square of all other freqs besides the 1kHz reference, in
  // dBr units (compared to magnitude of received reference). Using dBr (not
  // dBFS) includes level attenuation, making this metric a good proxy of
  // frequency-independent fidelity in our audio processing pipeline.
  static double FloorOutput8;
  static double FloorOutput16;
  static double FloorOutputFloat;

  static constexpr double kPrevFloorOutput8 = 45.920261;
  static constexpr double kPrevFloorOutput16 = 97.944722;
  static constexpr double kPrevFloorOutputFloat = 98.104753;

  // class is static only - prevent attempts to instantiate it
  AudioResult() = delete;

  // The subsequent methods are used when updating the kPrev threshold arrays to
  // match new (presumably improved) results. They display the current run's
  // results in an easily-imported format. Use the --dump flag to trigger this.
  static void DumpThresholdValues();

 private:
  static void DumpFreqRespValues(double* freq_resp_vals, std::string arr_name);
  static void DumpSinadValues(double* sinad_vals, std::string arr_name);
  static void DumpNoiseFloorValues();
  static void DumpLevelValues();
  static void DumpLevelToleranceValues();
  static void DumpDynamicRangeValues();
};

}  // namespace test
}  // namespace audio
}  // namespace media

/*
    AudioResult journal - updated upon each CL that affects these measurements

    2018-05-08  Added modulo & denominator parameters, to express resampling
                precision that cannot be captured by a single frac_step_size
                uint32. We can now send mix jobs of any size (even 64k) without
                accumulating position error.
                With this fix, our first round of audio fidelity improvements is
                complete. One remaining future focus could be to achieve flatter
                frequency response, presumably via a higher-order resampler.
    2018-05-01  Added new rate ratio for micro-SRC testing: 47999:48000. Also
                increased our mix job size to 20 ms (see 04-23 below), to better
                show the effects of accumulated fractional position errors.
    2018-04-30  Converted internal accumulator pipeline to 18-bit fixed-point
                rather than 16-bit. This will improve noise-floor and other
                measurements by up to 12 dB, in cases where quality is not gated
                by other factors (such as the bit-width of the input or output).
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
    2018-03-26  Initial dynamic range tests. kPrevScaleEpsilon = 0FFFFFFF for
                incoming positive values; 0FFFE000 for negative values.
    2018-03-21  Initial frequency response / sinad tests: 1kHz, 40Hz, 12kHz.
    2018-03-20  Initial source/output noise floor tests: 8- & 16-bit, 1kHz.
*/

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_TEST_AUDIO_RESULT_H_
