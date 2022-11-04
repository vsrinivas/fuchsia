// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_TEST_AUDIO_RESULT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_TEST_AUDIO_RESULT_H_

#include <cmath>

#include "src/media/audio/audio_core/v1/mixer/constants.h"
#include "src/media/audio/audio_core/v1/mixer/gain.h"
#include "src/media/audio/audio_core/v1/mixer/test/frequency_set.h"

namespace media::audio::test {

// Audio measurements that are determined by various test cases throughout the overall set. These
// measurements are eventually displayed in an overall recap, after all other tests have completed.
//
// We perform frequency tests at various frequencies (kSummaryFreqs[] from frequency_set.h), storing
// the result for each frequency.
//
// Although these audio measurements are quantitative, there is no 'right answer' per se. Rather, we
// compare current measurements to those previously measured, to detect any fidelity regressions.
// Because the code being tested is largely mathematical (only dependencies being a few FBL
// functions), we will fail on ANY regression, since presumably an intentional change in our
// fidelity would contain in that same CL a change to these thresholds.
//
// All reference values and measured values are in decibels (+20dB => 10x magn). When comparing
// values to the below limits, a specified 'tolerance' refers to the maximum delta (positive OR
// negative) from reference value. For ALL OTHER limits (Noise Floor, FrequencyResponse,
// SignalToNoiseAndDistortion), values being assessed should be **greater than or equal to** the
// specified limit.
//
// We save previous results to 8-digit accuracy (>23 bits), exceeding float32 precision. This does
// not pose a risk of 'flaky test' since the math should be the same every time. With no real
// dependencies outside FBL, we expect any change that affects these results to be directly within
// the core objects (Mixer, Gain, OutputProducer), and the corresponding adjustments to these
// thresholds should be included with that CL.
//
// Measurements and thresholds grouped into stages (where our pipeline is represented by the 6
// stages Input|Rechannel|Interpolate|Scale|Sum|Output).
class AudioResult {
 public:
  //
  //
  // Input
  //
  // For different input types (unsigned 8-bit int, signed-16, signed-24-in-32, float), we measure
  // the translation from input signal to what is generated and deposited into the accumulator
  // buffer.
  //
  // These variables store the worst-case difference (across multiple tests and frequencies) in
  // decibels, between an input's result and the reference dB level. For certain low-frequencies,
  // the frequency response exceeds 0 dBFS, and these variables store the worst-case measurement.
  static double LevelToleranceSource8;
  static double LevelToleranceSource16;
  static double LevelToleranceSource24;
  static double LevelToleranceSourceFloat;

  // Related to the above, these constants store the previous measurements. These are used as
  // threshold limits -- if any current test EXCEEDS this tolerance, it is considered an error and
  // causes the test case to fail.
  static constexpr double kPrevLevelToleranceSource8 = 6.4082082e-04;
  static constexpr double kPrevLevelToleranceSource16 = 6.8541681e-07;
  static constexpr double kPrevLevelToleranceSource24 = 3.0346074e-09;
  static constexpr double kPrevLevelToleranceSourceFloat = 5.3282082e-10;

  // These variables store the specific result magnitude (in dBFS) for the input type when a 1 kHz
  // reference-frequency full-scale 0 dBFS signal is provided.
  static double LevelSource8;
  static double LevelSource16;
  static double LevelSource24;
  static double LevelSourceFloat;

  // Related to the above, if the current measurement (0 dBFS sinusoid at a single reference
  // frequency) is LESS than the threshold constants listed below, it is considered an error and
  // causes the test case to fail.
  static constexpr double kPrevLevelSource8 = 0.0;
  static constexpr double kPrevLevelSource16 = 0.0;
  static constexpr double kPrevLevelSource24 = 0.0;
  static constexpr double kPrevLevelSourceFloat = 0.0;

  // Noise floor is assessed by injecting a full-scale 1 kHz sinusoid, then measuring the
  // root-sum-square strength of all the other frequencies besides 1 kHz. This strength is compared
  // to a full-scale signal, with the result being a positive dBr value representing the difference
  // between full-scale signal and noise floor. This test is performed at the same time as the above
  // level test (which uses that same 1 kHz reference frequency), in the absence of
  // rechannel/gain/SRC/mix.

  // These variables store the calculated dBFS noise floor for an input type. Here, a larger decibel
  // value is more desirable.
  static double FloorSource8;
  static double FloorSource16;
  static double FloorSource24;
  static double FloorSourceFloat;

  // These constants store previous noise floors per input type. Any current measurement LESS than
  // this threshold limit is considered a test failure.
  static constexpr double kPrevFloorSource8 = 49.952957;
  static constexpr double kPrevFloorSource16 = 98.104753;
  static constexpr double kPrevFloorSource24 = 146.30926;
  static constexpr double kPrevFloorSourceFloat = 153.74509;

  //
  //
  // Rechannel
  //
  // For mixer-provided rechannelization (currently just stereo-to-mono), we compare input signal to
  // generated result from rechannelization processing. We assess result level accuracy and noise
  // floor.

  // Worst-case measured tolerance, across potentially more than one test case.
  static double LevelToleranceStereoMono;
  // Previously-cached tolerance. If difference between input magnitude and result magnitude EXCEEDS
  // this tolerance, then the test case fails.
  static constexpr double kPrevLevelToleranceStereoMono = 6.0681545e-09;

  // Absolute output level measured in this test case.
  static double LevelStereoMono;
  // Previously-cached result that serves as a threshold limit. If result magnitude is LESS than
  // this value, then the test case fails.
  static constexpr double kPrevLevelStereoMono = -3.01029996;

  // Noise floor (in dBr) measured in this test case.
  static double FloorStereoMono;
  // Previously-cached noise floor that serves as a threshold limit. If result magnitude is LESS
  // than this value, then the test case fails.
  static constexpr double kPrevFloorStereoMono = 152.09879;

  //
  //
  // Interpolate
  //
  // We test interpolation fidelity using level response, SINAD and out-of-band rejection, and we do
  // this for all resamplers across a number of rate-conversion ratios and input frequencies. These
  // ratios are sometimes integral (e.g. 1:1, 2:1 or 1:2); others entail much larger numerators and
  // denominators(below referred to as "fractional" in nature). We use the following ratios:
  // - 1:1 (referred to in these variables and constants as Unity)
  // - 191999:48000, significant but not perfectly integral down-sampling (referred to as Down0)
  // - 2:1, which equates to 96k -> 48k (Down1)
  // - 294:160, which equates to 88.2k -> 48k (Down2)
  // - 48001:48000, representing small adjustment for multi-device sync (Micro)
  // - 147:160, which equates to 44.1k -> 48k (Up1)
  // - 1:2, which equates to 24k -> 48k, or 48k -> 96k (Up2)
  // - 12001:48000, significant but not perfectly integral up-sampling (Up3)
  //
  // For most audio fidelity tests, we test resamplers at each of these resampling ratios with a
  // broad range of "in-band" frequencies (from DC up to the Nyquist rate; as many as 40 freqs). For
  // certain tests, only one rate is used. For out-of-band rejection tests, a set of frequencies
  // beyond the Nyquist limit is used (currently 8).

  // Worst-case measured tolerance, across all test cases. Compared to performance on 1:1 ratios
  // (kLevelToleranceSourceFloat), sinc sampler boosts low-frequencies during any
  // up-sampling (e.g. 1:2), the WindowedSinc sampler significantly so (as much as 0.05 dB). In
  // effect, this const represents how far above 0 dBFS we allow for resampler frequency response.
  static double LevelToleranceInterpolation;
  // Previously-cached tolerance. If difference between input magnitude and result magnitude EXCEEDS
  // this tolerance, then the test case fails.
  static constexpr double kPrevLevelToleranceInterpolation = 5.4428201e-02;

  // Response (Frequency Response, Sinad, Phase Response)
  //
  // Frequency Response, Sinad and Phase testing uses expected values and tolerances. The expected
  // values are set by previous runs. For freq response and sinad, measured can always exceed
  // expected, but can also be less than expected if the delta is less than the tolerance.
  // For phase, measured must be within this tolerance of expected.
  static constexpr double kFreqRespTolerance = 0.001;
  static constexpr double kSinadTolerance = 0.001;
  static constexpr double kPhaseTolerance = 5e-06;

  // Frequency Response
  // What is our received level (in dBFS), when sending sinusoids through our mixer at certain
  // resampling ratios. Each resampler is specifically targeted with precise resampling ratios that
  // represent various ways that the system uses them. A more exhaustive set is available for
  // in-depth testing outside of CQ (if the "--full" switch is specified). Otherwise (in standard
  // mode), we test PointSampler at 1:1 (no SRC), 2:1 (96k-to-48k) and 1:2 (24k-to-48k), and
  // SincSampler at 294:160 (88.2k-to-48k), 48001:48000 ("micro-SRC") and 147:160 (44.1k-to-48k).
  // Our entire set of ratios is represented in the arrays listed below, referred to by these
  // labels: Unity (1:1), Down0 (191999:48000), Down1 (2:1), Down2 (294:160), Micro (48001:48000),
  // Up1 (147:160), Up2 (1:2) and Up3 (12001:48000).

  // For the specified resampler, and for specified rate conversion, these are currently-measured
  // results (in dBFS) for level response at a set of reference frequencies. The input sinusoid is
  // sent at 0 dBFS: thus, output results closer to 0 are better.
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> FreqRespPointUnity;

  // Same as the above, but for Windowed Sinc.
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> FreqRespSincUnity;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> FreqRespSincDown0;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> FreqRespSincDown1;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> FreqRespSincDown2;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> FreqRespSincMicro;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> FreqRespSincUp1;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> FreqRespSincUp2;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> FreqRespSincUp3;

  //
  // Val-being-checked (in dBFS) must equal or exceed this value. It also must not exceed 0.0dB by
  // more than kPrevLevelToleranceInterpolation. For 1:1 and N:1 ratios, PointSampler's frequency
  // response is ideal (flat). It is actually very slightly positive (hence the tolerance check).
  //
  // Note: with rates other than N:1 or 1:N, interpolating resamplers dampen high frequencies.
  //
  // These are previous-cached results for frequency response, for this sampler and rate ratio. If
  // any result magnitude is LESS than this value, the test case fails. Ideal results: 0.0 for all.
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevFreqRespUnity;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevFreqRespSincDown0;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevFreqRespSincDown1;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevFreqRespSincDown2;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevFreqRespSincMicro;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevFreqRespSincUp1;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevFreqRespSincUp2;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevFreqRespSincUp3;

  // Frequency Response results measured for a few frequencies during the NxN tests.
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> FreqRespSincNxN;

  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevFreqRespSincNxN;

  // Signal-to-Noise-And-Distortion (SINAD)
  // Sinad (signal-to-noise-and-distortion) is the ratio (in dBr) of received reference frequency
  // (nominally 1kHz), compared to power of all OTHER frequencies (combined via root-sum-square).
  //
  // Distortion is often measured at only one reference frequency. Where only a single frequency is
  // used (such as with noise floor testing), we use kReferenceFreq which refers to 1kHz. For
  // full-spectrum SINAD tests we use 47 frequencies. These arrays hold various SINAD results as
  // measured during the test run. For summary SINAD tests we use a subset of these frequencies,
  // kSummaryIdxs, which correspond to 40 Hz, 1 kHz and 12 kHz.
  //
  // For the specified resampler and conversion ratio, these are the currently-measured SINAD
  // results at a set of reference frequencies. The input sinusoid is a 0 dBFS sinusoid. The
  // measured outputs are twofold: the expected signal, and the combination of all other resultant
  // noise. Results are ratios of signal-to-other, measured in dBr, so larger values are better.
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadPointUnity;

  // Same as the above, but for Windowed Sinc.
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadSincUnity;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadSincDown0;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadSincDown1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadSincDown2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadSincMicro;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadSincUp1;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadSincUp2;
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadSincUp3;

  // Previous-cached SINAD results for this sampler and rate conversion ratio, in dBr. If current
  // result is LESS than this value, the test case fails.
  static const std::array<double, FrequencySet::kNumReferenceFreqs> kPrevSinadUnity;
  static const std::array<double, FrequencySet::kNumReferenceFreqs> kPrevSinadSincDown0;
  static const std::array<double, FrequencySet::kNumReferenceFreqs> kPrevSinadSincDown1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs> kPrevSinadSincDown2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs> kPrevSinadSincMicro;
  static const std::array<double, FrequencySet::kNumReferenceFreqs> kPrevSinadSincUp1;
  static const std::array<double, FrequencySet::kNumReferenceFreqs> kPrevSinadSincUp2;
  static const std::array<double, FrequencySet::kNumReferenceFreqs> kPrevSinadSincUp3;

  // SINAD results measured for a few frequencies during the NxN tests.
  static std::array<double, FrequencySet::kNumReferenceFreqs> SinadSincNxN;

  static const std::array<double, FrequencySet::kNumReferenceFreqs> kPrevSinadSincNxN;

  // Phase Response
  // What is the delay, from input to output, of various frequencies as signals go through our
  // resamplers? This characteristic of a system is called its phase response. Zero delay (phase
  // response 0) is ideal; constant delay (constant phase response) is excellent; linear response
  // (wrt frequency) is good. Phase response is measured in radians, for a given frequency.
  //
  // We display phase response within the range of (-PI, PI]. If a value lies outside that range,
  // 2PI is added to, or subtracted from, that value until it is within this range. Keeping this
  // "wraparound" in mind, we allow a certain phase tolerance when comparing to previous values.

  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> PhasePointUnity;

  // Same as the above, but for Windowed Sinc.
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> PhaseSincUnity;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> PhaseSincDown0;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> PhaseSincDown1;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> PhaseSincDown2;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> PhaseSincMicro;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> PhaseSincUp1;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> PhaseSincUp2;
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> PhaseSincUp3;

  // For Phase, measured value must exceed or equal the below cached value.
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevPhaseUnity;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevPhaseSincDown0;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevPhaseSincDown1;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevPhaseSincDown2;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevPhaseSincMicro;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevPhaseSincUp1;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevPhaseSincUp2;
  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevPhaseSincUp3;

  // Phase results measured for a few frequencies during the NxN tests.
  static std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> PhaseSincNxN;

  static const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> kPrevPhaseSincNxN;

  //
  //
  // Scale

  // The lowest (furthest-from-Unity) AScale with no observable attenuation on full-scale data (i.e.
  // the smallest AScale indistinguishable from Unity).
  //
  // This const is determined by the number of precision bits in float32. At this value or higher,
  // scaled values round back to original values.
  static constexpr float kMinGainDbUnity = -0.000000258856886667820f;
  //
  // The highest (closest-to-Unity) AScale with an observable effect on full-scale (i.e. the largest
  // sub-Unity AScale distinguishable from Unity).
  //
  // Related to kMinGainDbUnity, scaled by this gain_db or lower, 1.0 and -1.0 round to new values.
  static constexpr float kMaxGainDbNonUnity = -0.000000258865572365570f;
  //
  // Measured results for kMinGainDbUnity and kMaxGainDbNonUnity confirm what can be derived:
  // Ratio (2^25-1)/2^25, multiplied by full-scale (1.0) float, produces hex equivalent 0x0.FFFFFF8
  // Float lacks precision for the final "8" so the result will be rounded. Above this ratio, we are
  // indistinguishable from Unity. At less than this ratio -- at least for full-scale signals -- we
  // differ from Unity. MinGainUnity and MaxGainNonUnity are db values on EITHER side of this ratio.
  //

  // The lowest (closest-to-zero) AScale at which full-scale data are not silenced (i.e. the
  // smallest AScale that is distinguishable from Mute).
  //
  // This value would actually be infinitesimally close to zero, if it were not for our -160dB
  // limit. kMinGainDbNonMute is essentially kMutedGainDb -- plus the smallest-possible increment
  // that a float32 can express. Note the close relation to kMaxGainDbMute.
  static constexpr float kMinGainDbNonMute = -159.999992f;
  //
  // The highest (furthest-from-Mute) AScale at which full-scale data are silenced (i.e. the largest
  // AScale that is indistinguishable from Mute).
  //
  // This value would actually be infinitesimally close to zero, if it were not for our -160dB
  // limit. kMaxGainDbMute is essentially kMutedGainDb -- plus an increment that float32 ultimately
  // CANNOT express.
  static constexpr float kMaxGainDbMute = -159.999993f;
  //
  // Measured results for kMinGainDbNonMute and kMaxGainDbMute confirm what can be derived:
  // -160 in float is [mantissa: 1.25, binary exponent: 7]. Mantissa 1.25 is 0x1.400000 with a final
  // hex digit of 3 significant bits. "Half a float32 bit" here is that additional least significant
  // bit. Thus for float32, the dividing line between what IS and IS NOT distinguishable from
  // -160.0f has a mantissa of -0x1.3FFFFF.
  //
  // Reduced: kMinGainDbNonMute|kMaxGainDbMute should be just greater|less than this value:
  //
  //   -1    *    (2^24 + (2^22 - 1)) / 2^24    *    2^7
  //  sign        \------- mantissa -------/       exponent
  //

  static_assert(kMinGainDbUnity > kMaxGainDbNonUnity,
                "kMaxGainDbNonUnity should be distinguishable from Unity");
  static_assert(kMinGainDbNonMute > kMaxGainDbMute,
                "kMinGainDbNonMute should be distinguishable from Mute");

  // This is the worst-case value (measured potentially across multiple test cases) for how close we
  // can get to Unity scale while still causing different results than when using Unity scale.
  static Gain::AScale ScaleEpsilon;

  // This is the worst-case value (measured potentially across multiple test cases) for how close we
  // can get to zero scale (mute) while still causing a non-mute outcome.
  static Gain::AScale MinScaleNonZero;

  // Dynamic Range
  // (gain integrity and system response at low volume levels)
  //
  // Measured at a single reference frequency (kReferenceFreq), on a lone mono source without SRC.
  // By determining the smallest possible change in gain that causes a detectable change in output
  // (our 'gain epsilon'), we determine a system's sensitivity to gain changes. We measure not only
  // the output level of the signal, but also the noise level across all other frequencies.
  // Performing these same measurements (output level and noise level) with other gains as well
  // (-30dB, -60dB, -90dB) is the standard definition of Dynamic Range testing: adding these gains
  // to the measured signal-to-noise determines a system's usable data range (translatable into the
  // more accessible Effective Number Of Bits metric). Level measurements at these different gains
  // are useful not only as components of the "noise in the presence of signal" calculation, but
  // also as avenues toward measuring a system's linearity/accuracy/precision with regard to data
  // scaling and gain.
  //
  // The worst-case value (measured potentially across multiple test cases) for how far we diverge
  // from target amplitude levels in Dynamic Range testing.
  static double DynRangeTolerance;
  // The previous-cached worst-case tolerance value, for Dynamic range testing. If a current
  // tolerance EXCEEDS this value, then the test case fails.
  static constexpr double kPrevDynRangeTolerance = 4.6729294e-07;

  // Level, and unwanted artifacts -- as well as previously-cached threshold limits for the same --
  // when applying the smallest-detectable gain change.
  static double LevelEpsilonDown;
  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevLevelEpsilonDown = -2.5886558e-07;

  // Previously-cached sinad when applying the smallest-detectable gain change.
  static double SinadEpsilonDown;
  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevSinadEpsilonDown = 152.25480;

  // Level + unwanted artifacts -- and the previously-cached threshold limits for the same -- when
  // applying -30/-60/-90dB gain (measures dynamic range).
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
  // How close is a measured level to the reference dB level?  Val-being-checked must be within this
  // distance (above OR below) from the reference dB level.
  //
  // Worst-case value (measured potentially across multiple test cases) for how far our mix result
  // level diverges from the target amplitude in Mix testing.
  static double LevelToleranceMix8;
  static double LevelToleranceMix16;
  static double LevelToleranceMix24;
  static double LevelToleranceMixFloat;

  // If current tolerance EXCEEDS this value, then the test case fails.
  static constexpr double kPrevLevelToleranceMix8 = 6.4082082e-04;
  static constexpr double kPrevLevelToleranceMix16 = 6.8541681e-07;
  static constexpr double kPrevLevelToleranceMix24 = 3.0346074e-09;
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
  static constexpr double kPrevFloorMix24 = 146.30926;
  static constexpr double kPrevFloorMixFloat = 153.74509;

  //
  //
  // Output
  //
  // How close is a measured level to the reference dB level?  Val-being-checked must be within this
  // distance (above OR below) from the reference dB level.
  //
  // For this output type, this is the worst-case value for how far our output result level diverged
  // from the target amplitude in Output testing. This result is measured in dBr, for input signals
  // of 0 dBFS.
  static double LevelToleranceOutput8;
  static double LevelToleranceOutput16;
  static double LevelToleranceOutput24;
  static double LevelToleranceOutputFloat;

  // If current tolerance EXCEEDS this value, then the test case fails.
  static constexpr double kPrevLevelToleranceOutput8 = 6.4082082e-04;
  static constexpr double kPrevLevelToleranceOutput16 = 9.9668031e-07;
  static constexpr double kPrevLevelToleranceOutput24 = 3.0250373e-07;
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

  // What is our best-case noise floor in absence of rechannel/gain/SRC/mix. Val is root-sum-square
  // of all other freqs besides the 1kHz reference, in dBr units (compared to magnitude of received
  // reference). This is measured in dBr, relative to the expected full-scale output. Higher
  // positive values represent "quieter" output functions and are desired.
  static double FloorOutput8;
  static double FloorOutput16;
  static double FloorOutput24;
  static double FloorOutputFloat;

  // If current value is LESS than this value, then the test case fails.
  static constexpr double kPrevFloorOutput8 = 49.952957;
  static constexpr double kPrevFloorOutput16 = 98.104911;
  static constexpr double kPrevFloorOutput24 = 146.22129;
  static constexpr double kPrevFloorOutputFloat = 153.74509;

  // class is static only - prevent attempts to instantiate it
  AudioResult() = delete;

  //
  // The subsequent methods are used when updating the kPrev threshold arrays to match new
  // (presumably improved) results. They display the current run's results in an easily-imported
  // format. Use '--dump' to trigger this.
  //
  static void DumpThresholdValues();

 private:
  static void DumpFreqRespValues();
  static void DumpSinadValues();
  static void DumpPhaseValues();

  static void DumpNoiseFloorValues();
  static void DumpLevelValues();
  static void DumpLevelToleranceValues();
  static void DumpDynamicRangeValues();

  static void DumpFreqRespValueSet(double* freq_resp_vals, const std::string& arr_name);
  static void DumpSinadValueSet(double* sinad_vals, const std::string& arr_name);
  static void DumpPhaseValueSet(double* phase_vals, const std::string& arr_name);
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_TEST_AUDIO_RESULT_H_
