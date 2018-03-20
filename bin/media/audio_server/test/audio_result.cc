// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio_result.h"

namespace media {
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
// What is our best-case noise floor in absence of rechannel/gain/SRC/mix.
// Val is root-sum-square of all other freqs besides the 1kHz reference, in
// dBr units (compared to magnitude of received reference). Using dBr (not
// dBFS) includes level attenuation, making this metric a good proxy of
// frequency-independent fidelity in our audio processing pipeline.
double AudioResult::FloorSource8 = -INFINITY;
double AudioResult::FloorOutput8 = -INFINITY;
double AudioResult::FloorSource16 = -INFINITY;
double AudioResult::FloorOutput16 = -INFINITY;

// Val-being-checked (in dBr to reference signal) must be >= this value.
constexpr double AudioResult::kPrevFloorSource8;
constexpr double AudioResult::kPrevFloorOutput8;
constexpr double AudioResult::kPrevFloorSource16;
constexpr double AudioResult::kPrevFloorOutput16;

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

}  // namespace test
}  // namespace media
