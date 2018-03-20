// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio_result.h"
#include "gtest/gtest.h"

namespace media {
namespace test {

//
// These test functions, run after all other details tests have executed,
// producing a digest of the various audio fidelity measurements made.
//
// Display our best-case noise floor measurements, in decibels below full-scale
TEST(Recap, NoiseFloor) {
  // When the mixer accepts audio in 8-bit form (from AudioRenderer, for
  // example), this is our demonstrated best-case noise-floor measurement.
  printf("\n Baseline noise-floor for 8-bit sources:    %5.2lf dB (was %5.2lf)",
         AudioResult::FloorSource8, AudioResult::kPrevFloorSource8);

  // When the mixer accepts audio in 16-bit form (from AudioRenderer, for
  // example), this is our demonstrated best-case noise-floor measurement.
  printf("\n Baseline noise-floor for 16-bit sources:   %5.2lf dB (was %5.2lf)",
         AudioResult::FloorSource16, AudioResult::kPrevFloorSource16);

  // When the mixer produces and outputs audio in 8-bit form (to an audio
  // Output device, for example), this is our demonstrated best-case noise-floor
  printf("\n Baseline noise-floor for 8-bit outputs:    %5.2lf dB (was %5.2lf)",
         AudioResult::FloorOutput8, AudioResult::kPrevFloorOutput8);

  // When the mixer produces and outputs audio in 16-bit form (to an audio
  // Output device, for example), this is our demonstrated best-case noise-floor
  printf("\n Baseline noise-floor for 16-bit outputs:   %5.2lf dB (was %5.2lf)",
         AudioResult::FloorOutput16, AudioResult::kPrevFloorOutput16);

  printf("\n\n");
}

}  // namespace test
}  // namespace media
