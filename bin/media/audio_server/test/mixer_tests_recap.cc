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
  printf("\n Best-case noise-floor");
  printf("\n   (in dB, with prior results)");

  printf("\n\n   Sources");
  printf("\n\t     8-bit           16-bit");
  printf("\n\t %5.2lf  (%5.2lf)   %5.2lf  (%5.2lf)", AudioResult::FloorSource8,
         AudioResult::kPrevFloorSource8, AudioResult::FloorSource16,
         AudioResult::kPrevFloorSource16);

  printf("\n\n   Mix Floor");
  printf("\n\t     8-bit           16-bit");
  printf("\n\t %5.2lf  (%5.2lf)   %5.2lf  (%5.2lf)", AudioResult::FloorMix8,
         AudioResult::kPrevFloorMix8, AudioResult::FloorMix16,
         AudioResult::kPrevFloorMix16);

  printf("\n\n   Outputs");
  printf("\n\t     8-bit           16-bit");
  printf("\n\t %5.2lf  (%5.2lf)   %5.2lf  (%5.2lf)", AudioResult::FloorOutput8,
         AudioResult::kPrevFloorOutput8, AudioResult::FloorOutput16,
         AudioResult::kPrevFloorOutput16);

  printf("\n\n\n");
}

TEST(Recap, FreqResp) {
  printf("\n Frequency Response");
  printf("\n   (in dB, with prior results)");

  printf("\n\n   Point resampler");
  printf("\n\t\t        No SRC                  96k->48k");
  for (uint32_t freq = 0; freq < FrequencySet::kNumSummaryFreqs; ++freq) {
    printf("\n   %8u Hz   %9.6lf  (%9.6lf)   %9.6lf  (%9.6lf)",
           FrequencySet::kSummaryFreqsTranslated[freq],
           AudioResult::FreqRespPointUnity[freq],
           AudioResult::kPrevFreqRespPointUnity[freq],
           AudioResult::FreqRespPointDown[freq],
           AudioResult::kPrevFreqRespPointDown[freq]);
  }

  printf("\n\n   Linear resampler");
  printf("\n\t\t      88.2k->48k               44.1k->48k");
  for (uint32_t freq = 0; freq < FrequencySet::kNumSummaryFreqs; ++freq) {
    printf("\n   %8u Hz   %9.6lf  (%9.6lf)   %9.6lf  (%9.6lf)",
           FrequencySet::kSummaryFreqsTranslated[freq],
           AudioResult::FreqRespLinearDown[freq],
           AudioResult::kPrevFreqRespLinearDown[freq],
           AudioResult::FreqRespLinearUp[freq],
           AudioResult::kPrevFreqRespLinearUp[freq]);
  }

  printf("\n\n");
}

TEST(Recap, SINAD) {
  printf("\n Signal-to-Noise-and-Distortion (SINAD)");
  printf("\n   (in dB, with prior results)");

  printf("\n\n   Point resampler");
  printf("\n\t\t     No SRC          96k->48k");
  printf("\n   %8u Hz    %5.2lf  (%5.2lf)   %5.2lf  (%5.2lf)",
         FrequencySet::kSummaryFreqsTranslated[FrequencySet::kRefFreqBin],
         AudioResult::SinadPointUnity, AudioResult::kPrevSinadPointUnity,
         AudioResult::SinadPointDown, AudioResult::kPrevSinadPointDown);

  printf("\n\n   Linear resampler");
  printf("\n\t\t   88.2k->48k       44.1k->48k");
  printf("\n   %8u Hz    %5.2lf  (%5.2lf)   %5.2lf  (%5.2lf)",
         FrequencySet::kSummaryFreqsTranslated[FrequencySet::kRefFreqBin],
         AudioResult::SinadLinearDown, AudioResult::kPrevSinadLinearDown,
         AudioResult::SinadLinearUp, AudioResult::kPrevSinadLinearUp);

  printf("\n\n");
}

TEST(Recap, DynamicRange) {
  printf("\n Dynamic Range");
  printf("\n   (in dB, with prior results)");

  printf("\n\n      Input Gain       Mixed Result       Usable Range\n");
  printf("\n     -0.00000003   %8.4lf (%8.4lf)   %5.2lf (%5.2lf)",
         AudioResult::LevelDownEpsilon, AudioResult::kPrevLevelDownEpsilon,
         AudioResult::SinadDownEpsilon, AudioResult::kPrevSinadDownEpsilon);
  printf("\n    -60.0000       %8.4lf (%8.4lf)   %5.2lf (%5.2lf)",
         AudioResult::LevelDown60, -60.0 - AudioResult::kPrevDynRangeTolerance,
         AudioResult::SinadDown60, AudioResult::kPrevSinadDown60);
  printf("\n\n");
}

}  // namespace test
}  // namespace audio
}  // namespace media
