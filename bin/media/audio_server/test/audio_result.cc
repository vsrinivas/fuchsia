// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/audio_result.h"

namespace media {
namespace audio {
namespace test {

// Audio measurements that are determined by various test cases throughout the
// overall set. These measurements are eventually displayed in an overall recap,
// after all other tests have completed.

//
// How close is a measured level to the reference dB level?  Val-being-checked
// must be within this distance (above OR below) from the reference dB level.
constexpr double AudioResult::kLevelToleranceSource8;
constexpr double AudioResult::kLevelToleranceOutput8;
constexpr double AudioResult::kLevelToleranceSource16;
constexpr double AudioResult::kLevelToleranceOutput16;

//
// Purely when calculating gain (in dB) from gain_scale (fixed-point int),
// derived values must be within this multiplier (above or below) of target.
constexpr double AudioResult::kGainToleranceMultiplier;

//
// What is our best-case noise floor in absence of rechannel/gain/SRC/mix.
// Val is root-sum-square of all other freqs besides the 1kHz reference, in
// dBr units (compared to magnitude of received reference). Using dBr (not
// dBFS) includes level attenuation, making this metric a good proxy of
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
double AudioResult::FreqRespPointUnity[FrequencySet::kNumSummaryFreqs] = {
    -INFINITY, -INFINITY, -INFINITY};
double AudioResult::FreqRespPointDown[FrequencySet::kNumSummaryFreqs] = {
    -INFINITY, -INFINITY, -INFINITY};
double AudioResult::FreqRespLinearDown[FrequencySet::kNumSummaryFreqs] = {
    -INFINITY, -INFINITY, -INFINITY};
double AudioResult::FreqRespLinearUp[FrequencySet::kNumSummaryFreqs] = {
    -INFINITY, -INFINITY, -INFINITY};

// Val-being-checked (in dBFS) must be greater than or equal to this value.
//
// Note: with rates other than N:1 or 1:N, interpolating resamplers dampen
// high frequencies, as shown in the previously-saved LinearSampler results.
constexpr double
    AudioResult::kPrevFreqRespPointUnity[FrequencySet::kNumSummaryFreqs];
constexpr double
    AudioResult::kPrevFreqRespPointDown[FrequencySet::kNumSummaryFreqs];
constexpr double
    AudioResult::kPrevFreqRespLinearDown[FrequencySet::kNumSummaryFreqs];
constexpr double
    AudioResult::kPrevFreqRespLinearUp[FrequencySet::kNumSummaryFreqs];

//
// Distortion is measured at a single reference frequency (kReferenceFreq).
// Sinad (signal-to-noise-and-distortion) is the ratio (in dBr) of reference
// signal (nominally 1kHz) to the combined power of all OTHER frequencies.
double AudioResult::SinadPointUnity = -INFINITY;
double AudioResult::SinadPointDown = -INFINITY;
double AudioResult::SinadLinearDown = -INFINITY;
double AudioResult::SinadLinearUp = -INFINITY;

// Val-being-checked (in dBFS) must be greater than or equal to this value.
constexpr double AudioResult::kPrevSinadPointUnity;
constexpr double AudioResult::kPrevSinadPointDown;
constexpr double AudioResult::kPrevSinadLinearDown;
constexpr double AudioResult::kPrevSinadLinearUp;

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
