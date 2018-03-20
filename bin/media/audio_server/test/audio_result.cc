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

}  // namespace test
}  // namespace media
