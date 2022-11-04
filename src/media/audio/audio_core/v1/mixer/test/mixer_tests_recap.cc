// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/v1/mixer/test/mixer_tests_recap.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/v1/mixer/test/audio_result.h"
#include "src/media/audio/audio_core/v1/mixer/test/mixer_tests_shared.h"

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

  int32_t begin_idx, end_idx;
  if (FrequencySet::UseFullFrequencySet) {
    begin_idx = FrequencySet::kFirstInBandRefFreqIdx;
    end_idx = FrequencySet::kFirstOutBandRefFreqIdx;
  } else {
    begin_idx = 0u;
    end_idx = FrequencySet::kSummaryIdxs.size();
  }

  printf("\n\n   Point resampler\n           ");
  printf("         No SRC   ");

  for (auto idx = begin_idx; idx < end_idx; ++idx) {
    auto freq_idx = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6d Hz", FrequencySet::kRefFreqsTranslated[freq_idx]);
    if (AudioResult::kPrevFreqRespUnity[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespPointUnity[freq_idx],
             AudioResult::kPrevFreqRespUnity[freq_idx]);
    }
  }

  printf("\n\n   Windowed Sinc resampler\n           ");
  printf("         No SRC   ");
  printf("       191999->48k");
  printf("        96k->48k  ");
  printf("       88.2k->48k ");
  printf("        Micro-SRC ");
  printf("       44.1k->48k ");
  printf("        24k->48k  ");
  printf("       12001->48k ");

  for (auto idx = begin_idx; idx < end_idx; ++idx) {
    auto freq_idx = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6d Hz", FrequencySet::kRefFreqsTranslated[freq_idx]);

    if (AudioResult::kPrevFreqRespUnity[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincUnity[freq_idx],
             AudioResult::kPrevFreqRespUnity[freq_idx]);
    } else {
      printf("                  ");
    }

    if (AudioResult::kPrevFreqRespSincDown0[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincDown0[freq_idx],
             AudioResult::kPrevFreqRespSincDown0[freq_idx]);
    } else {
      printf("                  ");
    }

    if (AudioResult::kPrevFreqRespSincDown1[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincDown1[freq_idx],
             AudioResult::kPrevFreqRespSincDown1[freq_idx]);
    } else {
      printf("                  ");
    }

    if (AudioResult::kPrevFreqRespSincDown2[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincDown2[freq_idx],
             AudioResult::kPrevFreqRespSincDown2[freq_idx]);
    } else {
      printf("                  ");
    }

    if (AudioResult::kPrevFreqRespSincMicro[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincMicro[freq_idx],
             AudioResult::kPrevFreqRespSincMicro[freq_idx]);
    } else {
      printf("                  ");
    }

    if (AudioResult::kPrevFreqRespSincUp1[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincUp1[freq_idx],
             AudioResult::kPrevFreqRespSincUp1[freq_idx]);
    } else {
      printf("                  ");
    }

    if (AudioResult::kPrevFreqRespSincUp2[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincUp2[freq_idx],
             AudioResult::kPrevFreqRespSincUp2[freq_idx]);
    } else {
      printf("                  ");
    }

    if (AudioResult::kPrevFreqRespSincUp3[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf (%6.3lf)", AudioResult::FreqRespSincUp3[freq_idx],
             AudioResult::kPrevFreqRespSincUp3[freq_idx]);
    }
  }

  printf("\n\n");
}

void MixerTestsRecap::PrintSinadSummary() {
  printf("\n\n Signal-to-Noise-and-Distortion (SINAD)");
  printf("\n   (in dB, with prior results, more positive is better)");

  int32_t begin_idx, end_idx;
  if (FrequencySet::UseFullFrequencySet) {
    begin_idx = FrequencySet::kFirstInBandRefFreqIdx;
    end_idx = FrequencySet::kFirstOutBandRefFreqIdx;
  } else {
    begin_idx = 0u;
    end_idx = FrequencySet::kSummaryIdxs.size();
  }

  printf("\n\n   Point resampler\n           ");
  printf("          No SRC   ");

  for (auto idx = begin_idx; idx < end_idx; ++idx) {
    auto freq_idx = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6d Hz ", FrequencySet::kRefFreqsTranslated[freq_idx]);
    if (AudioResult::kPrevSinadUnity[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadPointUnity[freq_idx],
             AudioResult::kPrevSinadUnity[freq_idx]);
    }
  }

  printf("\n\n   Windowed Sinc resampler\n           ");
  printf("          No SRC   ");
  printf("        191999->48k");
  printf("         96k->48k  ");
  printf("        88.2k->48k ");
  printf("         Micro-SRC ");
  printf("        44.1k->48k ");
  printf("         24k->48k  ");
  printf("        12001->48k ");

  for (auto idx = begin_idx; idx < end_idx; ++idx) {
    auto freq_idx = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6d Hz ", FrequencySet::kRefFreqsTranslated[freq_idx]);

    if (AudioResult::kPrevSinadUnity[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincUnity[freq_idx],
             AudioResult::kPrevSinadUnity[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincDown0[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown0[freq_idx],
             AudioResult::kPrevSinadSincDown0[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincDown1[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown1[freq_idx],
             AudioResult::kPrevSinadSincDown1[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincDown2[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown2[freq_idx],
             AudioResult::kPrevSinadSincDown2[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincMicro[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincMicro[freq_idx],
             AudioResult::kPrevSinadSincMicro[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincUp1[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincUp1[freq_idx],
             AudioResult::kPrevSinadSincUp1[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincUp2[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincUp2[freq_idx],
             AudioResult::kPrevSinadSincUp2[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincUp3[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincUp3[freq_idx],
             AudioResult::kPrevSinadSincUp3[freq_idx]);
    }
  }

  printf("\n\n");
}

void MixerTestsRecap::PrintOutOfBandRejectionSummary() {
  printf("\n\n Out-of-band Rejection");
  printf("\n   (in dB, with prior results, more positive is better)");

  if (!FrequencySet::UseFullFrequencySet) {
    printf("\n\n   Results only generated during full-spectrum testing\n\n");
    return;
  }

  int32_t begin_idx = FrequencySet::kFirstOutBandRefFreqIdx;
  int32_t end_idx = FrequencySet::kReferenceFreqs.size();

  printf("\n\n   Windowed Sinc resampler\n           ");
  printf("        191999->48k");
  printf("         96k->48k  ");
  printf("        88.2k->48k ");
  printf("         Micro-SRC ");

  for (auto idx = begin_idx; idx < end_idx; ++idx) {
    int32_t freq_idx = idx;
    printf("\n   %6d Hz ", FrequencySet::kRefFreqsTranslated[freq_idx]);

    if (AudioResult::kPrevSinadSincDown0[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown0[freq_idx],
             AudioResult::kPrevSinadSincDown0[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincDown1[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown1[freq_idx],
             AudioResult::kPrevSinadSincDown1[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincDown2[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincDown2[freq_idx],
             AudioResult::kPrevSinadSincDown2[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevSinadSincMicro[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.2lf  (%6.2lf)", AudioResult::SinadSincMicro[freq_idx],
             AudioResult::kPrevSinadSincMicro[freq_idx]);
    }
  }

  printf("\n\n");
}

void MixerTestsRecap::PrintPhaseResponseSummary() {
  printf("\n Phase response");
  printf("\n   (in radians, with prior results, zero is ideal)");

  int32_t begin_idx, end_idx;
  if (FrequencySet::UseFullFrequencySet) {
    begin_idx = FrequencySet::kFirstInBandRefFreqIdx;
    end_idx = FrequencySet::kFirstOutBandRefFreqIdx;
  } else {
    begin_idx = 0u;
    end_idx = FrequencySet::kSummaryIdxs.size();
  }

  printf("\n\n   Point resampler\n           ");
  printf("          No SRC   ");

  for (auto idx = begin_idx; idx < end_idx; ++idx) {
    auto freq_idx = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6d Hz ", FrequencySet::kRefFreqsTranslated[freq_idx]);
    if (AudioResult::kPrevPhaseUnity[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhasePointUnity[freq_idx],
             AudioResult::kPrevPhaseUnity[freq_idx]);
    }
  }

  printf("\n\n   Windowed Sinc resampler\n           ");
  printf("          No SRC   ");
  printf("        191999->48k");
  printf("         96k->48k  ");
  printf("        88.2k->48k ");
  printf("         Micro-SRC ");
  printf("        44.1k->48k ");
  printf("         24k->48k  ");
  printf("        12001->48k ");

  for (auto idx = begin_idx; idx < end_idx; ++idx) {
    auto freq_idx = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6d Hz ", FrequencySet::kRefFreqsTranslated[freq_idx]);

    if (AudioResult::kPrevPhaseUnity[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincUnity[freq_idx],
             AudioResult::kPrevPhaseUnity[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevPhaseSincDown0[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincDown0[freq_idx],
             AudioResult::kPrevPhaseSincDown0[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevPhaseSincDown1[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincDown1[freq_idx],
             AudioResult::kPrevPhaseSincDown1[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevPhaseSincDown2[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincDown2[freq_idx],
             AudioResult::kPrevPhaseSincDown2[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevPhaseSincMicro[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincMicro[freq_idx],
             AudioResult::kPrevPhaseSincMicro[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevPhaseSincUp1[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincUp1[freq_idx],
             AudioResult::kPrevPhaseSincUp1[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevPhaseSincUp2[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincUp2[freq_idx],
             AudioResult::kPrevPhaseSincUp2[freq_idx]);
    } else {
      printf("                   ");
    }

    if (AudioResult::kPrevPhaseSincUp3[freq_idx] != -std::numeric_limits<double>::infinity()) {
      printf("   %6.3lf  (%6.3lf)", AudioResult::PhaseSincUp3[freq_idx],
             AudioResult::kPrevPhaseSincUp3[freq_idx]);
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
