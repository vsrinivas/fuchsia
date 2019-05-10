// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TEST_AUDIO_RESULT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TEST_AUDIO_RESULT_H_

#include <cmath>

#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/mixer/test/frequency_set.h"

namespace media::audio::test {

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
// objects (Mixer, Gain, OutputProducer), and the corresponding adjustments
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
  // For different input types (unsigned 8-bit int, signed-16, signed-24-in-32,
  // float), we measure the translation from input signal to what is generated
  // and deposited into the accumulator buffer.
  //
  // These variables store the worst-case difference (across multiple tests and
  // frequencies) in decibels, between an input's result and the reference dB
  // level. For certain low-frequencies, the frequency response exceeds 0 dBFS,
  // and these variables store the worst-case measurement.
  static double LevelToleranceSource8;
  static double LevelToleranceSource16;
  static double LevelToleranceSource24;
  static double LevelToleranceSourceFloat;

  // Related to the above, these constants store the previous measurements.
  // These are used as threshold limits -- if any current test EXCEEDS this
  // tolerance, it is considered an error and causes the test case to fail.
  static constexpr double kPrevLevelToleranceSource8 = 6.4082082e-04;
  static constexpr double kPrevLevelToleranceSource16 = 6.8541681e-07;
  static constexpr double kPrevLevelToleranceSource24 = 2.3196300e-09;
  static constexpr double kPrevLevelToleranceSourceFloat = 5.3282082e-10;

  // These variables store the specific result magnitude (in dBFS) for the input
  // type when a 1 kHz reference-frequency full-scale 0 dBFS signal is provided.
  static double LevelSource8;
  static double LevelSource16;
  static double LevelSource24;
  static double LevelSourceFloat;

  // Related to the above, if the current measurement (0 dBFS sinusoid at a
  // single reference frequency) is LESS than the threshold constants listed
  // below, it is considered an error and causes the test case to fail.
  static constexpr double kPrevLevelSource8 = 0.0;
  static constexpr double kPrevLevelSource16 = 0.0;
  static constexpr double kPrevLevelSource24 = 0.0;
  static constexpr double kPrevLevelSourceFloat = 0.0;

  // Noise floor is assessed by injecting a full-scale 1 kHz sinusoid, then
  // measuring the root-sum-square strength of all the other frequencies besides
  // 1 kHz. This strength is compared to a full-scale signal, with the result
  // being a positive dBr value representing the difference between full-scale
  // signal and noise floor. This test is performed at the same time as the
  // above level test (which uses that same 1 kHz reference frequency), in the
  // absence of rechannel/gain/SRC/mix.

  // These variables store the calculated dBFS noise floor for an input type.
  // Here, a larger decibel value is more desirable.
  static double FloorSource8;
  static double FloorSource16;
  static double FloorSource24;
  static double FloorSourceFloat;

  // These constants store previous noise floors per input type. Any current
  // measurement LESS than this threshold limit is considered a test failure.
  static constexpr double kPrevFloorSource8 = 49.952957;
  static constexpr double kPrevFloorSource16 = 98.104753;
  static constexpr double kPrevFloorSource24 = 153.71926;
  static constexpr double kPrevFloorSourceFloat = 153.74509;

  //
  //
  // Rechannel
  //
  // For mixer-provided rechannelization (currently just stereo-to-mono), we
  // compare input signal to generated result from rechannelization processing.
  // We assess result level accuracy and noise floor.

  // Worst-case measured tolerance, across potentially more than one test case.
  static double LevelToleranceStereoMono;
  // Previously-cached tolerance. If difference between input magnitude and
  // result magnitude EXCEEDS this tolerance, then the test case fails.
  static constexpr double kPrevLevelToleranceStereoMono = 6.0681545e-09;

  // Absolute output level measured in this test case.
  static double LevelStereoMono;
  // Previously-cached result that serves as a threshold limit. If result
  // magnitude is LESS than this value, then the test case fails.
  static constexpr double kPrevLevelStereoMono = -3.01029996;

  // Noise floor (in dBr) measured in this test case.
  static double FloorStereoMono;
  // Previously-cached noise floor that serves as a threshold limit. If result
  // magnitude is LESS than this value, then the test case fails.
  static constexpr double kPrevFloorStereoMono = 152.09879;

  //
  //
  // Interpolate
  //
  // Generally we test our interpolation fidelity using level response and
  // SINAD, and we do so for all resamplers across a number of rate-conversion
  // ratios and input frequencies. These rate-conversion ratios include:
  // - 1:1 (referred to in these variables and constants as Unity)
  // - 2:1, which equates to 96k -> 48k (referred to as Down1)
  // - 294:160, which equates to 88.2k -> 48k (Down2)
  // - 147:160, which equates to 44.1k -> 48k (Up1)
  // - 1:2, which equates to 24k -> 48k, or 48k -> 96k (Up2)
  // - 47999:48000, representing small adjustment for multi-device sync (Micro)
  //

  // Worst-case measured tolerance, across potentially more than one test
  // case. Compared to 1:1 accuracy (kLevelToleranceSourceFloat),
  // LinearSampler boosts low-frequencies during any significant up-sampling
  // (e.g. 1:2). In effect, this const represents how far above 0 dBFS we
  // allow for those (any) freqs.
  static double LevelToleranceInterpolation;
  // Previously-cached tolerance. If difference between input magnitude and
  // result magnitude EXCEEDS this tolerance, then the test case fails.
  static constexpr double kPrevLevelToleranceInterpolation = 6.5187815e-05;

  // Frequency Response
  //
  // What is our received level (in dBFS), when sending sinusoids through our
  // mixer at certain resampling ratios. PointSampler and LinearSampler are
  // specifically targeted with resampling ratios that represent how the current
  // system uses them. A more exhaustive set is available for in-depth testing
  // outside of CQ (--full switch). In standard mode, we test PointSampler at
  // 1:1 (no SRC) and 2:1 (96k-to-48k), and LinearSampler at 294:160 and 147:160
  // (e.g. 88.2k-to-48k and 44.1k-to-48k). Additional results are gathered if
  // '--full' is specified. Our entire set of ratios is represented in the
  // arrays listed below, referred to by these labels: Unity (1:1), Down1 (2:1),
  // Down2 (294:160), Up1 (147:160), Up2 (1:2) and Micro (47999:48000).

  // For the specified resampler, and for the specified rate conversion, these
  // are the currently-measured results (in dBFS) for level response at a set of
  // reference frequencies. The input sinusoid is sent at 0 dBFS: thus, output
  // results closer to 0 are better.
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointDown0;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespPointUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespPointUp2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespPointUp3;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespPointMicro;

  // Same as the above section, but for LinearSampler
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearDown0;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs>
      FreqRespLinearDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespLinearUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespLinearUp2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespLinearUp3;
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
  //
  // These are the previous-cached results for frequency response, for this
  // sampler and this rate conversion. If any result magnitude is LESS than this
  // value, then the test case fails. Ideal results are 0.0 for all.
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointUnity;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointDown0;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointDown1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointDown2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointUp1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointUp2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointUp3;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespPointMicro;

  // Same as the above section, but for LinearSampler
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearUnity;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearDown0;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearDown1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearDown2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearUp1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearUp2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearUp3;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevFreqRespLinearMicro;

  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespPointNxN;
  static std::array<double, FrequencySet::kNumReferenceFreqs> FreqRespLinearNxN;

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
  //
  // For the specified resampler, and for the specified rate conversion, these
  // are the currently-measured SINAD results at a set of reference frequencies.
  // The input sinusoid is a 0 dBFS sinusoid. Results are ratios of output-
  // -signal-to-output-noise, measured in dBr, so larger values are better.
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointDown0;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointUp2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointUp3;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointMicro;

  // Same as the above section, but for LinearSampler
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearDown0;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearUp2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearUp3;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearMicro;

  // Previous-cached SINAD results for this sampler and rate conversion ratio,
  // in dBr. If current result is LESS than this value, the test case fails.
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointUnity;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointDown0;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointDown1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointDown2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointUp1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointUp2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointUp3;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadPointMicro;

  // Same as the above section, but for LinearSampler
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearUnity;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearDown0;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearDown1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearDown2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearUp1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearUp2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearUp3;
  static const std::array<double, FrequencySet::kNumReferenceFreqs>
      kPrevSinadLinearMicro;

  // SINAD results measured for a few frequencies during the NxN tests.
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointNxN;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadLinearNxN;

  //
  //
  // Scale
  //
  // The lowest (furthest-from-Unity) AScale with no observable attenuation on
  // full-scale data (i.e. the smallest AScale indistinguishable from Unity).
  //
  // This const is determined by the number of precision bits in a float32.
  // Conceptually, it is exactly (2^25 - 1) / (2^25) -- float32 contains 25 bits
  // of precision, minus 1 for sign, plus 1 for rounding effects. At this value
  // exactly (or slightly more, if it cannot be perfectly expressed in float32),
  // all values effectly round back to their original values.
  static constexpr float kMinGainDbUnity = -0.000000258856886667820f;

  // The highest (closest-to-Unity) AScale with an observable effect on
  // full-scale (i.e. the largest sub-Unity AScale distinguishable from Unity).
  //
  // Related to kMinGainDbUnity, this const is also determined by the number of
  // precision bits in a float32. Conceptually, it is infinitesimally less than
  // (2^25 - 1) / (2^25).  At this value, for the first time the largest values
  // (i.e. full-scale) will not round back to their original values.
  static constexpr float kMaxGainDbNonUnity = -0.000000258865572365570f;

  // The lowest (closest-to-zero) AScale at which full-scale data are not
  // silenced (i.e. the smallest AScale that is distinguishable from Mute).
  //
  // This value would actually be infinitesimally close to zero, if it were not
  // for our -160dB limit. kMinGainDbNonMute is essentially kMutedGainDb -- plus
  // the smallest-possible increment that a float32 can express. Note the close
  // relation to kMaxGainDbMute.
  static constexpr float kMinGainDbNonMute = -159.99999f;

  // The highest (furthest-from-Mute) AScale at which full-scale data are
  // silenced (i.e. the largest AScale that is indistinguishable from Mute).
  //
  // This value would actually be infinitesimally close to zero, if it were not
  // for our -160dB limit. kMaxGainDbMute is essentially kMutedGainDb -- plus an
  // increment that float32 ultimately CANNOT express.
  static constexpr float kMaxGainDbMute = -159.999993f;

  static_assert(kMinGainDbUnity > kMaxGainDbNonUnity,
                "kMaxGainDbNonUnity should be distinguishable from Unity");
  static_assert(kMinGainDbNonMute > kMaxGainDbMute,
                "kMinGainDbNonMute should be distinguishable from Mute");

  // This is the worst-case value (measured potentially across multiple test
  // cases) for how close we can get to Unity scale while still causing
  // different results than when using Unity scale.
  static Gain::AScale ScaleEpsilon;

  // This is the worst-case value (measured potentially across multiple test
  // cases) for how close we can get to zero scale (mute) while still causing
  // a non-mute outcome.
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
  // level) with other gains as well (-30dB, -60dB, -90dB) is the standard
  // definition of Dynamic Range testing: adding these gains to the measured
  // signal-to-noise determines a system's usable data range (translatable into
  // the more accessible Effective Number Of Bits metric). Level measurements at
  // these different gains are useful not only as components of the "noise in
  // the presence of signal" calculation, but also as avenues toward measuring a
  // system's linearity/accuracy/precision with regard to data scaling and gain.
  //
  // The worst-case value (measured potentially across multiple test cases) for
  // how far we diverge from target amplitude levels in Dynamic Range testing.
  static double DynRangeTolerance;
  // The previous-cached worst-case tolerance value, for Dynamic range testing.
  // If a current tolerance EXCEEDS this value, then the test case fails.
  static constexpr double kPrevDynRangeTolerance = 4.6729294e-07;

  // Level and unwanted artifacts, applying the smallest-detectable gain change.
  static double LevelEpsilonDown;
  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevLevelEpsilonDown = -2.5886558e-07;

  // Previously-cached sinad when applying the smallest-detectable gain change.
  static double SinadEpsilonDown;
  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevSinadEpsilonDown = 152.25480;

  // Level and unwanted artifacts -- as well as previously-cached threshold
  // limits for the same -- when applying -30dB gain (measures dynamic range).
  static double Level30Down;
  static double Level60Down;
  static double Level90Down;

  // Current and previously-cached SINAD, when applying -30 / -60 / -90 dB gain.

  // Current measured SINAD at -30 gain.
  static double Sinad30Down;
  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevSinad30Down = 149.95967;

  // Current measured SINAD at -60 gain.
  static double Sinad60Down;
  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevSinad60Down = 149.69530;

  // Current measured SINAD at -90 gain.
  static double Sinad90Down;
  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevSinad90Down = 149.58577;

  //
  //
  // Sum
  //
  // How close is a measured level to the reference dB level?  Val-being-checked
  // must be within this distance (above OR below) from the reference dB level.
  //
  // Worst-case value (measured potentially across multiple test cases) for how
  // far our mix result level diverges from the target amplitude in Mix testing.
  static double LevelToleranceMix8;
  static double LevelToleranceMix16;
  static double LevelToleranceMix24;
  static double LevelToleranceMixFloat;

  // If current tolerance EXCEEDS this value, then the test case fails.
  static constexpr double kPrevLevelToleranceMix8 = 6.40820813e-04;
  static constexpr double kPrevLevelToleranceMix16 = 6.85416802e-07;
  static constexpr double kPrevLevelToleranceMix24 = 2.3196300e-09;
  static constexpr double kPrevLevelToleranceMixFloat = 5.3282082e-10;

  // Absolute output level (dBFS) measured in Mix tests for this input type.
  static double LevelMix8;
  static double LevelMix16;
  static double LevelMix24;
  static double LevelMixFloat;

  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevLevelMix8 = 0.0;
  static constexpr double kPrevLevelMix16 = 0.0;
  static constexpr double kPrevLevelMix24 = 0.0;
  static constexpr double kPrevLevelMixFloat = 0.0;

  // Noise floor (dBr to full-scale) measured in Mix tests for this input type.
  static double FloorMix8;
  static double FloorMix16;
  static double FloorMix24;
  static double FloorMixFloat;

  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevFloorMix8 = 49.952317;
  static constexpr double kPrevFloorMix16 = 98.104753;
  static constexpr double kPrevFloorMix24 = 153.71927;
  static constexpr double kPrevFloorMixFloat = 153.74509;

  //
  //
  // Output
  //
  // How close is a measured level to the reference dB level?  Val-being-checked
  // must be within this distance (above OR below) from the reference dB level.
  //
  // For this output type, this is the worst-case value for how far our output
  // result level diverged from the target amplitude in Output testing. This
  // result is measured in dBr, for input signals of 0 dBFS.
  static double LevelToleranceOutput8;
  static double LevelToleranceOutput16;
  static double LevelToleranceOutput24;
  static double LevelToleranceOutputFloat;

  // If current tolerance EXCEEDS this value, then the test case fails.
  static constexpr double kPrevLevelToleranceOutput8 = 6.4082082e-04;
  static constexpr double kPrevLevelToleranceOutput16 = 9.9668031e-07;
  static constexpr double kPrevLevelToleranceOutput24 = 2.5596549e-09;
  static constexpr double kPrevLevelToleranceOutputFloat = 5.3282082e-10;

  // Absolute output level (dBFS) measured in Output tests for this type.
  static double LevelOutput8;
  static double LevelOutput16;
  static double LevelOutput24;
  static double LevelOutputFloat;

  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevLevelOutput8 = 0.0;
  static constexpr double kPrevLevelOutput16 = 0.0;
  static constexpr double kPrevLevelOutput24 = 0.0;
  static constexpr double kPrevLevelOutputFloat = 0.0;

  // What is our best-case noise floor in absence of rechannel/gain/SRC/mix.
  // Val is root-sum-square of all other freqs besides the 1kHz reference, in
  // dBr units (compared to magnitude of received reference). This is measured
  // in dBr, relative to the expected full-scale output. Higher positive values
  // represent "quieter" output functions and are desired.
  static double FloorOutput8;
  static double FloorOutput16;
  static double FloorOutput24;
  static double FloorOutputFloat;

  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevFloorOutput8 = 49.952957;
  static constexpr double kPrevFloorOutput16 = 98.104911;
  static constexpr double kPrevFloorOutput24 = 153.72051;
  static constexpr double kPrevFloorOutputFloat = 153.74509;

  // class is static only - prevent attempts to instantiate it
  AudioResult() = delete;

  //
  // The subsequent methods are used when updating the kPrev threshold arrays to
  // match new (presumably improved) results. They display the current run's
  // results in an easily-imported format. Use '--dump' to trigger this.
  //
  static void DumpThresholdValues();

 private:
  static void DumpFreqRespValues();
  static void DumpSinadValues();
  static void DumpNoiseFloorValues();
  static void DumpLevelValues();
  static void DumpLevelToleranceValues();
  static void DumpDynamicRangeValues();

  static void DumpFreqRespValueSet(double* freq_resp_vals,
                                   const std::string& arr_name);
  static void DumpSinadValueSet(double* sinad_vals,
                                const std::string& arr_name);
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TEST_AUDIO_RESULT_H_
