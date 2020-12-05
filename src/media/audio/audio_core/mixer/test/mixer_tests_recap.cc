// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/test/mixer_tests_recap.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/test/audio_result.h"
#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"

namespace media::audio::test {

//
// These produce a digest of the results from our detailed audio fidelity tests.
//
void MixerTestsRecap::PrintFidelityResultsSummary() {
  PrintFrequencyResponseSummary();
  PrintSinadSummary();
  PrintOutOfBandRejectionSummary();
  PrintPhaseResponseSummary();
  PrintNoiseFloorSummary();
  PrintDynamicRangeSummary();
}

void MixerTestsRecap::PrintFrequencyResponseSummary() {
  printf("\n\n Frequency Response");
  printf("\n   (in dB, with prior results, zero is ideal)");

  uint32_t begin_idx, end_idx;
  if (FrequencySet::UseFullFrequencySet) {
    begin_idx = FrequencySet::kFirstInBandRefFreqIdx;
    end_idx = FrequencySet::kFirstOutBandRefFreqIdx;
  } else {
    begin_idx = 0u;
    end_idx = FrequencySet::kSummaryIdxs.size();
  }

  printf("\n\n   Point resampler\n           ");

  printf("         No SRC   ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("       191999->48k");
  }
  printf("        96k->48k  ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("       88.2k->48k ");
    printf("        Micro-SRC ");
    printf("       44.1k->48k ");
  }
  printf("        24k->48k  ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("       12001->48k ");
  }

  for (uint32_t idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6u Hz", FrequencySet::kRefFreqsTranslated[freq]);
    if (AudioResult::kPrevFreqRespPointUnity[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespPointUnity[freq],
             AudioResult::kPrevFreqRespPointUnity[freq]);
    } else {
      printf("                  ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespPointDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespPointDown0[freq],
               AudioResult::kPrevFreqRespPointDown0[freq]);
      } else {
        printf("                  ");
      }
    }

    if (AudioResult::kPrevFreqRespPointDown1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespPointDown1[freq],
             AudioResult::kPrevFreqRespPointDown1[freq]);
    } else {
      printf("                  ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespPointDown2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespPointDown2[freq],
               AudioResult::kPrevFreqRespPointDown2[freq]);
      } else {
        printf("                  ");
      }

      if (AudioResult::kPrevFreqRespPointMicro[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespPointMicro[freq],
               AudioResult::kPrevFreqRespPointMicro[freq]);
      } else {
        printf("                  ");
      }

      if (AudioResult::kPrevFreqRespPointUp1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespPointUp1[freq],
               AudioResult::kPrevFreqRespPointUp1[freq]);
      } else {
        printf("                  ");
      }
    }

    if (AudioResult::kPrevFreqRespPointUp2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespPointUp2[freq],
             AudioResult::kPrevFreqRespPointUp2[freq]);
    } else {
      printf("                  ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespPointUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespPointUp3[freq],
               AudioResult::kPrevFreqRespPointUp3[freq]);
      }
    }
  }

  printf("\n\n   Linear resampler\n           ");

  if (FrequencySet::UseFullFrequencySet) {
    printf("         No SRC   ");
    printf("       191999->48k");
    printf("        96k->48k  ");
  }
  printf("       88.2k->48k ");
  printf("        Micro-SRC ");
  printf("       44.1k->48k ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("        24k->48k  ");
    printf("       12001->48k ");
  }

  for (uint32_t idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6u Hz", FrequencySet::kRefFreqsTranslated[freq]);

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespLinearUnity[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespLinearUnity[freq],
               AudioResult::kPrevFreqRespLinearUnity[freq]);
      } else {
        printf("                  ");
      }

      if (AudioResult::kPrevFreqRespLinearDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespLinearDown0[freq],
               AudioResult::kPrevFreqRespLinearDown0[freq]);
      } else {
        printf("                  ");
      }

      if (AudioResult::kPrevFreqRespLinearDown1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespLinearDown1[freq],
               AudioResult::kPrevFreqRespLinearDown1[freq]);
      } else {
        printf("                  ");
      }
    }

    if (AudioResult::kPrevFreqRespLinearDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespLinearDown2[freq],
             AudioResult::kPrevFreqRespLinearDown2[freq]);
    } else {
      printf("                  ");
    }

    if (AudioResult::kPrevFreqRespLinearMicro[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespLinearMicro[freq],
             AudioResult::kPrevFreqRespLinearMicro[freq]);
    } else {
      printf("                  ");
    }

    if (AudioResult::kPrevFreqRespLinearUp1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespLinearUp1[freq],
             AudioResult::kPrevFreqRespLinearUp1[freq]);
    } else {
      printf("                  ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespLinearUp2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespLinearUp2[freq],
               AudioResult::kPrevFreqRespLinearUp2[freq]);
      } else {
        printf("                  ");
      }

      if (AudioResult::kPrevFreqRespLinearUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespLinearUp3[freq],
               AudioResult::kPrevFreqRespLinearUp3[freq]);
      }
    }
  }

  printf("\n\n   Windowed Sinc resampler\n           ");

  if (FrequencySet::UseFullFrequencySet) {
    printf("         No SRC   ");
    printf("       191999->48k");
    printf("        96k->48k  ");
  }
  printf("       88.2k->48k ");
  printf("        Micro-SRC ");
  printf("       44.1k->48k ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("        24k->48k  ");
    printf("       12001->48k ");
  }

  for (uint32_t idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6u Hz", FrequencySet::kRefFreqsTranslated[freq]);

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespSincUnity[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincUnity[freq],
               AudioResult::kPrevFreqRespSincUnity[freq]);
      } else {
        printf("                  ");
      }

      if (AudioResult::kPrevFreqRespSincDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincDown0[freq],
               AudioResult::kPrevFreqRespSincDown0[freq]);
      } else {
        printf("                  ");
      }

      if (AudioResult::kPrevFreqRespSincDown1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincDown1[freq],
               AudioResult::kPrevFreqRespSincDown1[freq]);
      } else {
        printf("                  ");
      }
    }

    if (AudioResult::kPrevFreqRespSincDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincDown2[freq],
             AudioResult::kPrevFreqRespSincDown2[freq]);
    } else {
      printf("                  ");
    }

    if (AudioResult::kPrevFreqRespSincMicro[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincMicro[freq],
             AudioResult::kPrevFreqRespSincMicro[freq]);
    } else {
      printf("                  ");
    }

    if (AudioResult::kPrevFreqRespSincUp1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincUp1[freq],
             AudioResult::kPrevFreqRespSincUp1[freq]);
    } else {
      printf("                  ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespSincUp2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincUp2[freq],
               AudioResult::kPrevFreqRespSincUp2[freq]);
      } else {
        printf("                  ");
      }

      if (AudioResult::kPrevFreqRespSincUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincUp3[freq],
               AudioResult::kPrevFreqRespSincUp3[freq]);
      }
    }
  }

  printf("\n\n");
}

void MixerTestsRecap::PrintSinadSummary() {
  printf("\n\n Signal-to-Noise-and-Distortion (SINAD)");
  printf("\n   (in dB, with prior results, more positive is better)");

  uint32_t begin_idx, end_idx;
  if (FrequencySet::UseFullFrequencySet) {
    begin_idx = FrequencySet::kFirstInBandRefFreqIdx;
    end_idx = FrequencySet::kFirstOutBandRefFreqIdx;
  } else {
    begin_idx = 0u;
    end_idx = FrequencySet::kSummaryIdxs.size();
  }

  printf("\n\n   Point resampler\n           ");

  printf("          No SRC   ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("        191999->48k");
  }
  printf("         96k->48k  ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("        88.2k->48k ");
    printf("         Micro-SRC ");
    printf("        44.1k->48k ");
  }
  printf("         24k->48k  ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("        12001->48k ");
  }

  for (uint32_t idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6u Hz ", FrequencySet::kRefFreqsTranslated[freq]);
    if (AudioResult::kPrevSinadPointUnity[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointUnity[freq],
             AudioResult::kPrevSinadPointUnity[freq]);
    } else {
      printf("                   ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadPointDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointDown0[freq],
               AudioResult::kPrevSinadPointDown0[freq]);
      } else {
        printf("                   ");
      }
    }

    if (AudioResult::kPrevSinadPointDown1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointDown1[freq],
             AudioResult::kPrevSinadPointDown1[freq]);
    } else {
      printf("                   ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadPointDown2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointDown2[freq],
               AudioResult::kPrevSinadPointDown2[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevSinadPointMicro[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointMicro[freq],
               AudioResult::kPrevSinadPointMicro[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevSinadPointUp1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointUp1[freq],
               AudioResult::kPrevSinadPointUp1[freq]);
      } else {
        printf("                   ");
      }
    }

    if (AudioResult::kPrevSinadPointUp2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointUp2[freq],
             AudioResult::kPrevSinadPointUp2[freq]);
    } else {
      printf("                   ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadPointUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointUp3[freq],
               AudioResult::kPrevSinadPointUp3[freq]);
      }
    }
  }

  printf("\n\n   Linear resampler\n           ");

  if (FrequencySet::UseFullFrequencySet) {
    printf("          No SRC   ");
    printf("        191999->48k");
    printf("         96k->48k  ");
  }
  printf("        88.2k->48k ");
  printf("         Micro-SRC ");
  printf("        44.1k->48k ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("         24k->48k  ");
    printf("        12001->48k ");
  }

  for (uint32_t idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6u Hz ", FrequencySet::kRefFreqsTranslated[freq]);

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadLinearUnity[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearUnity[freq],
               AudioResult::kPrevSinadLinearUnity[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevSinadLinearDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown0[freq],
               AudioResult::kPrevSinadLinearDown0[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevSinadLinearDown1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown1[freq],
               AudioResult::kPrevSinadLinearDown1[freq]);
      } else {
        printf("                   ");
      }
    }

    if (AudioResult::kPrevSinadLinearDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown2[freq],
             AudioResult::kPrevSinadLinearDown2[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadLinearMicro[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearMicro[freq],
             AudioResult::kPrevSinadLinearMicro[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadLinearUp1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearUp1[freq],
             AudioResult::kPrevSinadLinearUp1[freq]);
    } else {
      printf("                   ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadLinearUp2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearUp2[freq],
               AudioResult::kPrevSinadLinearUp2[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevSinadLinearUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearUp3[freq],
               AudioResult::kPrevSinadLinearUp3[freq]);
      }
    }
  }

  printf("\n\n   Windowed Sinc resampler\n           ");

  if (FrequencySet::UseFullFrequencySet) {
    printf("          No SRC   ");
    printf("        191999->48k");
    printf("         96k->48k  ");
  }
  printf("        88.2k->48k ");
  printf("         Micro-SRC ");
  printf("        44.1k->48k ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("         24k->48k  ");
    printf("        12001->48k ");
  }

  for (uint32_t idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6u Hz ", FrequencySet::kRefFreqsTranslated[freq]);

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadSincUnity[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincUnity[freq],
               AudioResult::kPrevSinadSincUnity[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevSinadSincDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown0[freq],
               AudioResult::kPrevSinadSincDown0[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevSinadSincDown1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown1[freq],
               AudioResult::kPrevSinadSincDown1[freq]);
      } else {
        printf("                   ");
      }
    }

    if (AudioResult::kPrevSinadSincDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown2[freq],
             AudioResult::kPrevSinadSincDown2[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincMicro[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincMicro[freq],
             AudioResult::kPrevSinadSincMicro[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincUp1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincUp1[freq],
             AudioResult::kPrevSinadSincUp1[freq]);
    } else {
      printf("                   ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadSincUp2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincUp2[freq],
               AudioResult::kPrevSinadSincUp2[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevSinadSincUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincUp3[freq],
               AudioResult::kPrevSinadSincUp3[freq]);
      }
    }
  }

  printf("\n\n");
}

void MixerTestsRecap::PrintOutOfBandRejectionSummary() {
  printf("\n\n Out-of-band Rejection");
  printf("\n   (in dB, with prior results, more positive is better)");

  if (!FrequencySet::UseFullFrequencySet) {
    printf("\n\n   Results only given for full-spectrum testing\n\n");
    return;
  }

  uint32_t begin_idx = FrequencySet::kFirstOutBandRefFreqIdx;
  uint32_t end_idx = FrequencySet::kReferenceFreqs.size();

  printf("\n\n   Point resampler\n           ");

  printf("        191999->48k");
  printf("         96k->48k  ");
  printf("        88.2k->48k ");
  printf("         Micro-SRC ");

  for (auto idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = idx;

    printf("\n   %6u Hz ", FrequencySet::kRefFreqsTranslated[freq]);
    if (AudioResult::kPrevSinadPointDown0[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointDown0[freq],
             AudioResult::kPrevSinadPointDown0[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadPointDown1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointDown1[freq],
             AudioResult::kPrevSinadPointDown1[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadPointDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointDown2[freq],
             AudioResult::kPrevSinadPointDown2[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadPointMicro[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointMicro[freq],
             AudioResult::kPrevSinadPointMicro[freq]);
    }
  }

  printf("\n\n   Linear resampler\n           ");

  printf("        191999->48k");
  printf("         96k->48k  ");
  printf("        88.2k->48k ");
  printf("         Micro-SRC ");

  for (auto idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = idx;
    printf("\n  %6u Hz ", FrequencySet::kRefFreqsTranslated[freq]);

    if (AudioResult::kPrevSinadLinearDown0[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown0[freq],
             AudioResult::kPrevSinadLinearDown0[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadLinearDown1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown1[freq],
             AudioResult::kPrevSinadLinearDown1[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadLinearDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown2[freq],
             AudioResult::kPrevSinadLinearDown2[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadLinearMicro[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadLinearMicro[freq],
             AudioResult::kPrevSinadLinearMicro[freq]);
    }
  }

  printf("\n\n   Windowed Sinc resampler\n           ");

  printf("        191999->48k");
  printf("         96k->48k  ");
  printf("        88.2k->48k ");
  printf("         Micro-SRC ");

  for (auto idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = idx;
    printf("\n   %6u Hz ", FrequencySet::kRefFreqsTranslated[freq]);

    if (AudioResult::kPrevSinadSincDown0[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown0[freq],
             AudioResult::kPrevSinadSincDown0[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincDown1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown1[freq],
             AudioResult::kPrevSinadSincDown1[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown2[freq],
             AudioResult::kPrevSinadSincDown2[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincMicro[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincMicro[freq],
             AudioResult::kPrevSinadSincMicro[freq]);
    }
  }

  printf("\n\n");
}

void MixerTestsRecap::PrintPhaseResponseSummary() {
  printf("\n Phase response");
  printf("\n   (in radians, with prior results, zero is ideal)");

  uint32_t begin_idx, end_idx;
  if (FrequencySet::UseFullFrequencySet) {
    begin_idx = FrequencySet::kFirstInBandRefFreqIdx;
    end_idx = FrequencySet::kFirstOutBandRefFreqIdx;
  } else {
    begin_idx = 0u;
    end_idx = FrequencySet::kSummaryIdxs.size();
  }

  printf("\n\n   Point resampler\n           ");

  printf("          No SRC   ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("        191999->48k");
  }
  printf("         96k->48k  ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("        88.2k->48k ");
    printf("         Micro-SRC ");
    printf("        44.1k->48k ");
  }
  printf("         24k->48k  ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("        12001->48k ");
  }

  for (uint32_t idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6u Hz ", FrequencySet::kRefFreqsTranslated[freq]);
    if (AudioResult::kPrevPhasePointUnity[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhasePointUnity[freq],
             AudioResult::kPrevPhasePointUnity[freq]);
    } else {
      printf("                   ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevPhasePointDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhasePointDown0[freq],
               AudioResult::kPrevPhasePointDown0[freq]);
      } else {
        printf("                   ");
      }
    }

    if (AudioResult::kPrevPhasePointDown1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhasePointDown1[freq],
             AudioResult::kPrevPhasePointDown1[freq]);
    } else {
      printf("                   ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevPhasePointDown2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhasePointDown2[freq],
               AudioResult::kPrevPhasePointDown2[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevPhasePointMicro[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhasePointMicro[freq],
               AudioResult::kPrevPhasePointMicro[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevPhasePointUp1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhasePointUp1[freq],
               AudioResult::kPrevPhasePointUp1[freq]);
      } else {
        printf("                   ");
      }
    }

    if (AudioResult::kPrevPhasePointUp2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhasePointUp2[freq],
             AudioResult::kPrevPhasePointUp2[freq]);
    } else {
      printf("                   ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevPhasePointUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhasePointUp3[freq],
               AudioResult::kPrevPhasePointUp3[freq]);
      }
    }
  }

  printf("\n\n   Linear resampler\n           ");

  if (FrequencySet::UseFullFrequencySet) {
    printf("          No SRC   ");
    printf("        191999->48k");
    printf("         96k->48k  ");
  }
  printf("        88.2k->48k ");
  printf("         Micro-SRC ");
  printf("        44.1k->48k ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("         24k->48k  ");
    printf("        12001->48k ");
  }

  for (uint32_t idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6u Hz ", FrequencySet::kRefFreqsTranslated[freq]);

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevPhaseLinearUnity[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseLinearUnity[freq],
               AudioResult::kPrevPhaseLinearUnity[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevPhaseLinearDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseLinearDown0[freq],
               AudioResult::kPrevPhaseLinearDown0[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevPhaseLinearDown1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseLinearDown1[freq],
               AudioResult::kPrevPhaseLinearDown1[freq]);
      } else {
        printf("                   ");
      }
    }

    if (AudioResult::kPrevPhaseLinearDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseLinearDown2[freq],
             AudioResult::kPrevPhaseLinearDown2[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevPhaseLinearMicro[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseLinearMicro[freq],
             AudioResult::kPrevPhaseLinearMicro[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevPhaseLinearUp1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseLinearUp1[freq],
             AudioResult::kPrevPhaseLinearUp1[freq]);
    } else {
      printf("                   ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevPhaseLinearUp2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseLinearUp2[freq],
               AudioResult::kPrevPhaseLinearUp2[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevPhaseLinearUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseLinearUp3[freq],
               AudioResult::kPrevPhaseLinearUp3[freq]);
      }
    }
  }

  printf("\n\n   Windowed Sinc resampler\n           ");

  if (FrequencySet::UseFullFrequencySet) {
    printf("          No SRC   ");
    printf("        191999->48k");
    printf("         96k->48k  ");
  }
  printf("        88.2k->48k ");
  printf("         Micro-SRC ");
  printf("        44.1k->48k ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("         24k->48k  ");
    printf("        12001->48k ");
  }

  for (uint32_t idx = begin_idx; idx < end_idx; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6u Hz ", FrequencySet::kRefFreqsTranslated[freq]);

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevPhaseSincUnity[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincUnity[freq],
               AudioResult::kPrevPhaseSincUnity[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevPhaseSincDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincDown0[freq],
               AudioResult::kPrevPhaseSincDown0[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevPhaseSincDown1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincDown1[freq],
               AudioResult::kPrevPhaseSincDown1[freq]);
      } else {
        printf("                   ");
      }
    }

    if (AudioResult::kPrevPhaseSincDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincDown2[freq],
             AudioResult::kPrevPhaseSincDown2[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevPhaseSincMicro[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincMicro[freq],
             AudioResult::kPrevPhaseSincMicro[freq]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevPhaseSincUp1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincUp1[freq],
             AudioResult::kPrevPhaseSincUp1[freq]);
    } else {
      printf("                   ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevPhaseSincUp2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincUp2[freq],
               AudioResult::kPrevPhaseSincUp2[freq]);
      } else {
        printf("                   ");
      }

      if (AudioResult::kPrevPhaseSincUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincUp3[freq],
               AudioResult::kPrevPhaseSincUp3[freq]);
      }
    }
  }

  printf("\n\n");
}

//
// Display our baseline noise floor measurements, in decibels below full-scale
//
// 'Source' noise floor is the demonstrated best-case background noise when accepting audio (from an
// AudioRenderer or audio Input device, for example). 'Output' noise floor is the demonstrated
// best-case background noise when emitting audio (to an audio Output device or AudioCapturer, for
// example).
void MixerTestsRecap::PrintNoiseFloorSummary() {
  printf("\n\n Best-case noise-floor");
  printf("\n   (in dB, with prior results, higher is better)");

  printf("\n\n   Sources");
  printf("\n\t    8-bit    ");
  printf("        16-bit   ");
  printf("        24-bit   ");
  printf("        Float");
  printf("\n\t%6.2lf (%6.2lf)  %6.2lf (%6.2lf)  ", AudioResult::FloorSource8,
         AudioResult::kPrevFloorSource8, AudioResult::FloorSource16,
         AudioResult::kPrevFloorSource16);
  printf("%6.2lf (%6.2lf)  %6.2lf (%6.2lf)", AudioResult::FloorSource24,
         AudioResult::kPrevFloorSource24, AudioResult::FloorSourceFloat,
         AudioResult::kPrevFloorSourceFloat);

  printf("\n\n   Mix Floor");
  printf("\n\t    8-bit    ");
  printf("        16-bit   ");
  printf("        24-bit   ");
  printf("        Float    ");
  printf("     Stereo->Mono");
  printf("\n\t%6.2lf (%6.2lf)  %6.2lf (%6.2lf)  %6.2lf (%6.2lf)  ", AudioResult::FloorMix8,
         AudioResult::kPrevFloorMix8, AudioResult::FloorMix16, AudioResult::kPrevFloorMix16,
         AudioResult::FloorMix24, AudioResult::kPrevFloorMix24);
  printf("%6.2lf (%6.2lf)  %6.2lf (%6.2lf)", AudioResult::FloorMixFloat,
         AudioResult::kPrevFloorMixFloat, AudioResult::FloorStereoMono,
         AudioResult::kPrevFloorStereoMono);

  printf("\n\n   Outputs");
  printf("\n\t    8-bit    ");
  printf("        16-bit   ");
  printf("        24-bit   ");
  printf("        Float");
  printf("\n\t%6.2lf (%6.2lf)  %6.2lf (%6.2lf)  ", AudioResult::FloorOutput8,
         AudioResult::kPrevFloorOutput8, AudioResult::FloorOutput16,
         AudioResult::kPrevFloorOutput16);
  printf("%6.2lf (%6.2lf)  %6.2lf (%6.2lf)", AudioResult::FloorOutput24,
         AudioResult::kPrevFloorOutput24, AudioResult::FloorOutputFloat,
         AudioResult::kPrevFloorOutputFloat);

  printf("\n\n");
}

//
// Display our gain sensitivity and dynamic range, in decibels
//
void MixerTestsRecap::PrintDynamicRangeSummary() {
  printf("\n\n Dynamic Range");
  printf("\n   (in dB, with prior results, higher is better)");

  printf("\n\n     Input Gain       Mixed Result           Usable Range\n");
  printf("\n     %9.6lf  %10.6lf ( > %9.6lf)   %6.2lf (%6.2lf)", AudioResult::kMaxGainDbNonUnity,
         AudioResult::LevelEpsilonDown, AudioResult::kPrevLevelEpsilonDown,
         AudioResult::SinadEpsilonDown, AudioResult::kPrevSinadEpsilonDown);
  printf("\n    -30.0000    %8.4lf   (+/- %6.4lf  )   %6.2lf (%6.2lf)", AudioResult::Level30Down,
         AudioResult::kPrevDynRangeTolerance, AudioResult::Sinad30Down,
         AudioResult::kPrevSinad30Down);
  printf("\n    -60.0000    %8.4lf   (+/- %6.4lf  )   %6.2lf (%6.2lf)", AudioResult::Level60Down,
         AudioResult::kPrevDynRangeTolerance, AudioResult::Sinad60Down,
         AudioResult::kPrevSinad60Down);
  printf("\n    -90.0000    %8.4lf   (+/- %6.4lf  )   %6.2lf (%6.2lf)", AudioResult::Level90Down,
         AudioResult::kPrevDynRangeTolerance, AudioResult::Sinad90Down,
         AudioResult::kPrevSinad90Down);
  printf("\n\n");
}

}  // namespace media::audio::test
