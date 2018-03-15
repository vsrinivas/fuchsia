// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mixer_tests_shared.h"

namespace media {
namespace test {

//
// Audio measurements determined by various test cases throughout the set and
// eventually displayed in a final recap after other tests complete.
//

// Best-case noise floor in absence of rechannelization, rate change or gain.
// Recorded for incoming 8-bit, outgoing 8-bit, incoming 16-bit, outgoing 16-bit
double floor_8bit_source, floor_8bit_output;
double floor_16bit_source, floor_16bit_output;

//
// These test functions, run after all other details tests have executed,
// producing a digest of the various audio fidelity measurements made.
//
// Display our best-case noise floor measurements, in decibels below full-scale
TEST(Recap, NoiseFloor) {
  // When the mixer accepts audio in 8-bit form (from AudioRenderer, for
  // example), this is our demonstrated best-case noise-floor measurement.
  printf("\n Baseline noise-floor for 8-bit sources:  %.2lf dB",
         floor_8bit_source);

  // When the mixer accepts audio in 16-bit form (from AudioRenderer, for
  // example), this is our demonstrated best-case noise-floor measurement.
  printf("\n Baseline noise-floor for 16-bit sources: %.2lf dB",
         floor_16bit_source);

  // When the mixer produces and outputs audio in 8-bit form (to an audio
  // Output device, for example), this is our demonstrated best-case noise-floor
  printf("\n Baseline noise-floor for 8-bit outputs:  %.2lf dB",
         floor_8bit_output);

  // When the mixer produces and outputs audio in 16-bit form (to an audio
  // Output device, for example), this is our demonstrated best-case noise-floor
  printf("\n Baseline noise-floor for 16-bit outputs: %.2lf dB",
         floor_16bit_output);

  printf("\n\n");
}

}  // namespace test
}  // namespace media
