// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/audio_result.h"
#include "gtest/gtest.h"

namespace media {
namespace audio {
namespace test {

//
// These test functions, run after all other details tests have executed,
// producing a digest of the various audio fidelity measurements made.
//
// Display our baseline noise floor measurements, in decibels below full-scale
//
// 'Source' noise floor is the demonstrated best-case background noise when
// accepting audio (from an AudioRenderer or audio Input device, for example).
// 'Output' noise floor is the demonstrated best-case background noise when
// emitting audio (to an audio Output device or AudioCapturer, for example).
TEST(Recap, NoiseFloor) {
  printf("\n Best-case noise-floor\n   (no gain/SRC)");

  printf("\n\t\t\t   Sources\t\t   Outputs\n\t\t");
  for (uint32_t type = 0; type < 2; ++type) {
    printf("      8-bit       16-bit");
  }

  printf("\n\t\t      ");
  printf("%5.2lf dB    %5.2lf dB    %5.2lf dB    %5.2lf dB",
         AudioResult::FloorSource8, AudioResult::FloorSource16,
         AudioResult::FloorOutput8, AudioResult::FloorOutput16);

  printf("\n\t   (prior)    ");
  printf("%5.2lf       %5.2lf       %5.2lf       %5.2lf",
         AudioResult::kPrevFloorSource8, AudioResult::kPrevFloorSource16,
         AudioResult::kPrevFloorOutput8, AudioResult::kPrevFloorOutput16);

  printf("\n\n\n");
}

}  // namespace test
}  // namespace audio
}  // namespace media
