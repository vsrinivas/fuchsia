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
  static constexpr double kLevelToleranceSource16 = 0.00000068542196;
  static constexpr double kLevelToleranceOutput16 = 0.00000068541681;
  static constexpr double kLevelToleranceInterp16 = 0.00000080781106;

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

  // Val-being-checked (in dBr to reference signal) must be >= this value.
  static constexpr double kPrevFloorSource8 = 49.952957;
  static constexpr double kPrevFloorMix8 = 49.952957;
  static constexpr double kPrevFloorOutput8 = 45.920261;
  static constexpr double kPrevFloorSource16 = 98.104753;
  static constexpr double kPrevFloorMix16 = 90.677331;
  static constexpr double kPrevFloorOutput16 = 98.104753;

  static double LevelMix8;
  static double LevelMix16;

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
  static double FreqRespPointUnity[FrequencySet::kNumReferenceFreqs];
  static double FreqRespPointDown[FrequencySet::kNumReferenceFreqs];
  static double FreqRespLinearDown[FrequencySet::kNumReferenceFreqs];
  static double FreqRespLinearUp[FrequencySet::kNumReferenceFreqs];

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
      {  0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,              -INFINITY,        -INFINITY,
        -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,         -INFINITY          };
  static constexpr double kPrevFreqRespPointDown[]  =
      {  0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0,              0.0,
         0.0,              0.0,              0.0,              0.0,               0.0               };
  static constexpr double kPrevFreqRespLinearDown[] =
      {  0.0,              0.0,             -0.0000010765944, -0.00000011222767, -0.0000020938986, -0.0000018778418,
        -0.0000094623437, -0.0000098927249, -0.000014065467,  -0.000022771550,   -0.000037785192,  -0.000054824344,
        -0.000095210661,  -0.00014667886,   -0.00022827781,   -0.00036619075,    -0.00057611855,   -0.00091960698,
        -0.0014540418,    -0.0023462583,    -0.0036589951,    -0.0057131220,     -0.0093914625,    -0.014676537,
        -0.022955636,     -0.036462171,     -0.058795450,     -0.091922681,      -0.14606405,      -0.23562194,
        -0.36871115,      -0.53193138,      -0.95022508,      -1.4196069,        -1.4947992,       -1.5718087,
        -1.6511036,       -1.8235774,       -2.0597837,       -2.1695306,        -2.3593078,       -3.8104597,
        -6.3343783,       -7.8036133,       -7.8427753,       -INFINITY,         -INFINITY          };
  static constexpr double kPrevFreqRespLinearUp[]   =
      {  0.0,             -0.0000023504212, -0.0000043544860, -0.0000056409654,  -0.000010595969,  -0.000013453493,
        -0.000026934650,  -0.000035439283,  -0.000056430585,  -0.000094066218,   -0.00014871579,   -0.00022482655,
        -0.00038486095,   -0.00057909159,   -0.00091727461,   -0.0014642418,     -0.0023078001,    -0.0036759177,
        -0.0058170869,    -0.0093834696,    -0.014647070,     -0.022867357,      -0.037581601,     -0.058746770,
        -0.091919936,     -0.14606728,      -0.23572517,      -0.36897083,       -0.58739390,      -0.95056709,
        -1.4946470,       -2.1693107,       -3.9380418,       -5.9982547,        -6.3364566,       -6.6853228,
        -7.0472276,       -7.8442173,       -INFINITY,        -INFINITY,         -INFINITY,        -INFINITY,
        -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,        - INFINITY          };
  // clang-format on

  //
  // Distortion is measured at a single reference frequency (kReferenceFreq).
  // Sinad (signal-to-noise-and-distortion) is the ratio (in dBr) of reference
  // signal (nominally 1kHz) to the combined power of all OTHER frequencies.
  static double SinadPointUnity[FrequencySet::kNumReferenceFreqs];
  static double SinadPointDown[FrequencySet::kNumReferenceFreqs];
  static double SinadLinearDown[FrequencySet::kNumReferenceFreqs];
  static double SinadLinearUp[FrequencySet::kNumReferenceFreqs];

  // clang-format off
  static constexpr double kPrevSinadPointUnity[] = {
      98.104753, 98.092846, 98.104753, 98.104753, 98.092846, 98.104753,
      98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
      98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
      98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
      98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
      98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
      98.104753, 98.104753, 98.104753, 98.104753, -INFINITY, -INFINITY,
      -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY};

  static constexpr double kPrevSinadPointDown[] = {
      98.104753,            98.092846,            98.104753,
      98.104753,            98.092846,            98.104753,
      98.104753,            98.104753,            98.104753,
      98.104753,            98.104753,            98.104753,
      98.104753,            98.104753,            98.104753,
      98.104753,            98.104753,            98.104753,
      98.104753,            98.104753,            98.104753,
      98.104753,            98.104753,            98.104753,
      98.104753,            98.104753,            98.104753,
      98.104753,            98.104753,            98.104753,
      98.104753,            98.104753,            98.104753,
      98.104753,            98.104753,            98.104753,
      98.104753,            98.104753,            98.104753,
      98.104753,            -0.00000000067190481, -0.00000000067187492,
      -0.00000000067185563, -0.00000000067184599, -0.00000000067185852,
      -0.00000000067184695, -0.00000000067184599};

  static constexpr double kPrevSinadLinearDown[] = {
      98.104753, 93.124218,  93.076842, 93.089388, 93.087849,   93.104014,
      93.124252, 93.090645,  93.09431,  93.042959, 93.054275,   92.991397,
      92.87592,  92.646527,  92.008977, 90.782039, 88.678501,   85.663296,
      82.149077, 78.222449,  74.421795, 70.595109, 66.291866,   62.410875,
      58.517504, 54.480815,  50.300839, 46.376093, 42.282661,   38.012863,
      33.952617, 30.563039,  25.010493, 20.970874, 20.436069,   19.911664,
      19.394056, 18.336941,  17.017179, 14.391131, -0.12037547, -0.42761185,
      -1.743604, -3.0325247, -3.071563, -INFINITY, -INFINITY};

  static constexpr double kPrevSinadLinearUp[] = {
      98.104753, 93.128846,   93.096975, 93.096829, 93.107059, 93.060076,
      93.052275, 93.072997,   92.992189, 92.857695, 92.586523, 92.039931,
      90.468576, 88.599321,   85.677088, 82.109717, 78.343967, 74.403325,
      70.441937, 66.297342,   62.428449, 58.548117, 54.215684, 50.308188,
      46.376423, 42.282509,   38.008959, 33.946106, 29.63232,  25.006935,
      20.437138, 16.447282,   9.4396456, 3.8398891, 3.0577226, 2.2800661,
      1.5015966, -0.12452049, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
      -INFINITY, -INFINITY,   -INFINITY, -INFINITY, -INFINITY};
  // clang-format on

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

  // Level and unwanted artifacts, applying the smallest-detectable gain change.
  static double LevelDownEpsilon;
  static double SinadDownEpsilon;

  // Level and unwanted artifacts, applying -60dB gain (measures dynamic range).
  static double LevelDown60;
  static double SinadDown60;

  static constexpr double kPrevLevelDownEpsilon = -0.00016807164;
  static constexpr double kPrevDynRangeTolerance = 0.004922;

  static constexpr double kPrevSinadDownEpsilon = 93.232593;
  static constexpr double kPrevSinadDown60 = 34.196374;

  // class is static only - prevent attempts to instantiate it
  AudioResult() = delete;
};

}  // namespace test
}  // namespace audio
}  // namespace media
