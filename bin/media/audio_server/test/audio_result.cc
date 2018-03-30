// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/audio_result.h"

namespace media {
namespace audio {
namespace test {

// These audio measurements are measured by various test cases throughout the
// set. They are eventually displayed in a recap after all other tests complete.

//
// How close is a measured level to the reference dB level?  Val-being-checked
// must be within this distance (above OR below) from the reference dB level.
constexpr double AudioResult::kLevelToleranceSource8;
constexpr double AudioResult::kLevelToleranceOutput8;
constexpr double AudioResult::kLevelToleranceSource16;
constexpr double AudioResult::kLevelToleranceOutput16;
constexpr double AudioResult::kLevelToleranceInterp16;

//
// Purely when calculating gain (in dB) from gain_scale (fixed-point int),
// derived values must be within this multiplier (above or below) of target.
constexpr double AudioResult::kGainToleranceMultiplier;

//
// What is our best-case noise floor in absence of rechannel/gain/SRC/mix. Val
// is root-sum-square of all other freqs besides the 1kHz reference, in dBr
// units (compared to magnitude of received reference). Using dBr (not dBFS)
// includes level attenuation, making this metric a good proxy of
// frequency-independent fidelity in our audio processing pipeline.
double AudioResult::FloorSource8 = -INFINITY;
double AudioResult::FloorMix8 = -INFINITY;
double AudioResult::FloorOutput8 = -INFINITY;
double AudioResult::FloorSource16 = -INFINITY;
double AudioResult::FloorMix16 = -INFINITY;
double AudioResult::FloorOutput16 = -INFINITY;

// Val-being-checked (in dBr to reference signal) must be >= this value.
constexpr double AudioResult::kPrevFloorSource8;
constexpr double AudioResult::kPrevFloorMix8;
constexpr double AudioResult::kPrevFloorOutput8;
constexpr double AudioResult::kPrevFloorSource16;
constexpr double AudioResult::kPrevFloorMix16;
constexpr double AudioResult::kPrevFloorOutput16;

double AudioResult::LevelMix8 = -INFINITY;
double AudioResult::LevelMix16 = -INFINITY;

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
// We perform frequency response tests at various frequencies (kSummaryFreqs[]
// from frequency_set.h), storing the result at each frequency. As with
// resampling ratios, subsequent CL contains a more exhaustive frequency set,
// for in-depth testing and diagnostics to be done outside CQ.

// clang-format off
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::FreqRespPointUnity = {
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY   };

std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::FreqRespPointDown = {
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY   };

std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::FreqRespLinearDown = {
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY   };
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::FreqRespLinearUp = {
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY   };
// clang-format on

// Val-being-checked (in dBFS) must be greater than or equal to this value.
//
// Note: with rates other than N:1 or 1:N, interpolating resamplers dampen high
// frequencies, as shown in previously-saved LinearSampler results.

// clang-format off
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUnity = {
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,      -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown = {
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown = {
         0.0,              0.0,             -0.0000010765944, -0.0000023500457,  -0.0000023204115, -0.0000024819551,
        -0.0000094623437, -0.0000098927249, -0.000015352092,  -0.000024634046,   -0.000037785192,  -0.000055401688,
        -0.000095210661,  -0.00014667886,   -0.00023037397,   -0.00036630747,    -0.00057694728,   -0.00091971656,
        -0.0014540418,    -0.0023462583,    -0.0036597335,    -0.0057131220,     -0.0093914625,    -0.014676537,
        -0.022958352,     -0.036462171,     -0.058796479,     -0.091924166,      -0.14606405,      -0.23562194,
        -0.36871115,      -0.53193309,      -0.95022584,      -1.4196076,        -1.4948005,       -1.5718115,
        -1.6511036,       -1.8235781,       -2.0597837,       -2.1695351,        -2.3593078,       -3.8104597,
        -6.3343783,       -7.8036133,       -7.8427753,       -INFINITY,         -INFINITY          };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp = {
         0.0,             -0.0000023504212, -0.0000043544860, -0.0000058073382,  -0.000010595969,  -0.000013453493,
        -0.000028152172,  -0.000035439283,  -0.000056947810,  -0.000094066218,   -0.00014871579,   -0.00022498862,
        -0.00038578026,   -0.00057974394,   -0.00091727461,   -0.0014643869,     -0.0023078001,    -0.0036776787,
        -0.0058170869,    -0.0093836438,    -0.014647070,     -0.022867596,      -0.037581764,     -0.058746770,
        -0.091922190,     -0.14606728,      -0.23572614,      -0.36897083,       -0.58739390,      -0.95056709,
        -1.4946470,       -2.1693107,       -3.9380418,       -5.9982547,        -6.3364566,       -6.6853228,
        -7.0472288,       -7.8442190,       -INFINITY,        -INFINITY,         -INFINITY,        -INFINITY,
        -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,        - INFINITY          };
// clang-format on

//
// Distortion is measured at a single reference frequency (kReferenceFreq).
// Sinad (signal-to-noise-and-distortion) is the ratio (in dBr) of reference
// signal (nominally 1kHz) to the combined power of all OTHER frequencies.
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointUnity;
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointDown;
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearDown;
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadLinearUp;

// Val-being-checked (in dBFS) must be greater than or equal to this value.

// clang-format off
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUnity = {
        98.104753, 98.092846, 98.104753, 98.104753, 98.092846, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointDown = {
        98.104753, 98.092846, 98.104753, 98.104753, 98.092846, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, -0.00000000067190481,
        -0.00000000067187492, -0.00000000067185563, -0.00000000067184599,
        -0.00000000067185852, -0.00000000067184695, -0.00000000067184599 };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearDown = {
      98.104753, 93.124218,  93.076842, 93.089388, 93.087849,   93.104014,
      93.124252, 93.090645,  93.094310, 93.042959, 93.054275,   92.991397,
      92.875920, 92.646527,  92.008977, 90.782039, 88.678501,   85.663296,
      82.149077, 78.222449,  74.421795, 70.595109, 66.291866,   62.410875,
      58.517504, 54.480815,  50.300839, 46.375885, 42.282581,   38.012863,
      33.952617, 30.563039,  25.010451, 20.970874, 20.436067,   19.911664,
      19.394056, 18.336941,  17.017172, 14.391131, -0.12037547, -0.42761185,
      -1.743604, -3.0325247, -3.071563, -INFINITY, -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp = {
      98.104753, 93.128846,   93.096975, 93.096829, 93.107059, 93.060076,
      93.052275, 93.072997,   92.992189, 92.857695, 92.586523, 92.039931,
      90.468576, 88.599321,   85.677088, 82.109717, 78.343967, 74.403325,
      70.441937, 66.297342,   62.428449, 58.548117, 54.215625, 50.308188,
      46.376339, 42.282366,   38.008890, 33.946106, 29.632310, 25.006935,
      20.437118, 16.447273,    9.4396395, 3.8398891, 3.0577226, 2.2800661,
      1.5015886, -0.12452049, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
      -INFINITY, -INFINITY,   -INFINITY, -INFINITY, -INFINITY   };
// clang-format on

//
// Dynamic Range (gain integrity and system response at low volume levels) is
// measured at a single reference frequency (kReferenceFreq), on a lone mono
// source without SRC. By determining the smallest possible change in gain that
// causes a detectable change in output (our 'gain epsilon'), we determine a
// system's sensitivity to gain changes. We measure not only the output level of
// the signal, but also the noise level across all other frequencies. Performing
// these same measurements (output level and noise level) with a gain of -60 dB
// as well is the standard definition of Dynamic Range testing: by adding 60 dB
// to the measured signal-to-noise, one determines a system's usable range of
// data values (translatable into the more accessible Effective Number Of Bits
// metric). The level measurement at -60 dB is useful not only as a component of
// the "noise in the presence of signal" calculation, but also as a second
// avenue toward measuring a system's linearity/accuracy/precision with regard
// to data scaling and gain.

// Level and unwanted artifacts, applying the smallest-detectable gain change.
double AudioResult::LevelDownEpsilon = -INFINITY;
double AudioResult::SinadDownEpsilon = -INFINITY;

// Level and unwanted artifacts, applying -60dB gain (measures dynamic range).
double AudioResult::LevelDown60 = -INFINITY;
double AudioResult::SinadDown60 = -INFINITY;

// Level-being-checked (in dBFS) should be within kLevelToleranceSource16 of the
// dB gain setting. For SINAD, value-being-checked (in dBr, output signal to all
// other frequencies) must be greater than or equal to the below cached value.
constexpr double AudioResult::kPrevLevelDownEpsilon;
constexpr double AudioResult::kPrevDynRangeTolerance;

constexpr double AudioResult::kPrevSinadDownEpsilon;
constexpr double AudioResult::kPrevSinadDown60;

}  // namespace test
}  // namespace audio
}  // namespace media
