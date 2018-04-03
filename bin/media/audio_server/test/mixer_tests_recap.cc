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
  printf("\n\t     8-bit           16-bit         Stereo->Mono");
  printf("\n\t %5.2lf  (%5.2lf)   %5.2lf  (%5.2lf)   %5.2lf  (%5.2lf)",
         AudioResult::FloorMix8, AudioResult::kPrevFloorMix8,
         AudioResult::FloorMix16, AudioResult::kPrevFloorMix16,
         AudioResult::FloorStereoMono, AudioResult::kPrevFloorStereoMono);

  printf("\n\n   Outputs");
  printf("\n\t     8-bit           16-bit            Float");
  printf("\n\t %5.2lf  (%5.2lf)   %5.2lf  (%5.2lf)   %5.2lf  (%5.2lf)",
         AudioResult::FloorOutput8, AudioResult::kPrevFloorOutput8,
         AudioResult::FloorOutput16, AudioResult::kPrevFloorOutput16,
         AudioResult::FloorOutputFloat, AudioResult::kPrevFloorOutputFloat);

  printf("\n\n");
}

TEST(Recap, FreqResp) {
  printf("\n Frequency Response");
  printf("\n   (in dB, with prior results)");

  printf("\n\n   Point resampler");
  printf("\n\t\t          No SRC                    96k->48k");
  uint32_t num_freqs = FrequencySet::UseFullFrequencySet
                           ? FrequencySet::kReferenceFreqs.size()
                           : FrequencySet::kSummaryIdxs.size();
  for (uint32_t idx = 0; idx < num_freqs; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet
                        ? idx
                        : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %8u Hz", FrequencySet::kRefFreqsTranslated[freq]);
    if (AudioResult::kPrevFreqRespPointUnity[freq] !=
        -std::numeric_limits<double>::infinity()) {
      printf("   %11.6lf  (%9.6lf)", AudioResult::FreqRespPointUnity[freq],
             AudioResult::kPrevFreqRespPointUnity[freq]);
    } else {
      printf("                           ");
    }
    if (AudioResult::kPrevFreqRespPointDown[freq] !=
        -std::numeric_limits<double>::infinity()) {
      printf("   %11.6lf  (%9.6lf)", AudioResult::FreqRespPointDown[freq],
             AudioResult::kPrevFreqRespPointDown[freq]);
    }
  }

  printf("\n\n   Linear resampler");
  printf("\n\t\t        88.2k->48k                 44.1k->48k");
  for (uint32_t idx = 0; idx < num_freqs; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet
                        ? idx
                        : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %8u Hz", FrequencySet::kRefFreqsTranslated[freq]);
    if (AudioResult::kPrevFreqRespLinearDown[freq] !=
        -std::numeric_limits<double>::infinity()) {
      printf("   %11.6lf  (%9.6lf)", AudioResult::FreqRespLinearDown[freq],
             AudioResult::kPrevFreqRespLinearDown[freq]);
    } else {
      printf("                           ");
    }
    if (AudioResult::kPrevFreqRespLinearUp[freq] !=
        -std::numeric_limits<double>::infinity()) {
      printf("   %11.6lf  (%9.6lf)", AudioResult::FreqRespLinearUp[freq],
             AudioResult::kPrevFreqRespLinearUp[freq]);
    }
  }
  printf("\n\n");
}

TEST(Recap, SINAD) {
  printf("\n Signal-to-Noise-and-Distortion (SINAD)");
  printf("\n   (in dB, with prior results)");

  printf("\n\n   Point resampler");
  printf("\n\t\t       No SRC            96k->48k");
  uint32_t num_freqs = FrequencySet::UseFullFrequencySet
                           ? FrequencySet::kReferenceFreqs.size()
                           : FrequencySet::kSummaryIdxs.size();
  for (uint32_t idx = 0; idx < num_freqs; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet
                        ? idx
                        : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %8u Hz ", FrequencySet::kRefFreqsTranslated[freq]);
    if (AudioResult::kPrevSinadPointUnity[freq] !=
        -std::numeric_limits<double>::infinity()) {
      printf("     %5.2lf  (%5.2lf)", AudioResult::SinadPointUnity[freq],
             AudioResult::kPrevSinadPointUnity[freq]);
    } else {
      printf("                   ");
    }
    if (AudioResult::kPrevSinadPointDown[freq] !=
        -std::numeric_limits<double>::infinity()) {
      printf("     %5.2lf  (%5.2lf)", AudioResult::SinadPointDown[freq],
             AudioResult::kPrevSinadPointDown[freq]);
    }
  }

  printf("\n\n   Linear resampler");
  printf("\n\t\t     88.2k->48k         44.1k->48k");
  for (uint32_t idx = 0; idx < num_freqs; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet
                        ? idx
                        : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %8u Hz ", FrequencySet::kRefFreqsTranslated[freq]);
    if (AudioResult::kPrevSinadLinearDown[freq] !=
        -std::numeric_limits<double>::infinity()) {
      printf("   %7.2lf  (%5.2lf)", AudioResult::SinadLinearDown[freq],
             AudioResult::kPrevSinadLinearDown[freq]);
    } else {
      printf("                   ");
    }
    if (AudioResult::kPrevSinadLinearUp[freq] !=
        -std::numeric_limits<double>::infinity()) {
      printf("   %7.2lf  (%5.2lf)", AudioResult::SinadLinearUp[freq],
             AudioResult::kPrevSinadLinearUp[freq]);
    }
  }

  printf("\n\n");
}

TEST(Recap, DynamicRange) {
  printf("\n Dynamic Range");
  printf("\n   (in dB, with prior results)");

  printf("\n\n      Input Gain       Mixed Result          Usable Range\n");
  printf("\n     -0.000133  %10.6lf ( > %9.6lf)   %5.2lf (%5.2lf)",
         AudioResult::LevelEpsilonDown, AudioResult::kPrevLevelEpsilonDown,
         AudioResult::SinadEpsilonDown, AudioResult::kPrevSinadEpsilonDown);
  printf("\n    -60.0000    %8.4lf   (+/- %6.4lf  )   %5.2lf (%5.2lf)",
         AudioResult::Level60Down, AudioResult::kPrevDynRangeTolerance,
         AudioResult::Sinad60Down, AudioResult::kPrevSinad60Down);
  printf("\n\n");
}

}  // namespace test
}  // namespace audio
}  // namespace media
