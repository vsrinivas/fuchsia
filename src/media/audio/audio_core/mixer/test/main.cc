// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/media/audio/audio_core/mixer/test/audio_performance.h"
#include "src/media/audio/audio_core/mixer/test/audio_result.h"
#include "src/media/audio/audio_core/mixer/test/frequency_set.h"

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (!fxl::SetTestSettings(command_line)) {
    return EXIT_FAILURE;
  }

  // --full     Display results for the full frequency spectrum.
  // --dump     Display results in importable format.
  //            This flag is used when updating AudioResult kPrev arrays.
  // --profile  Profile the performance of Mix() across numerous configurations.
  bool show_full_frequency_set = command_line.HasOption("full");
  bool dump_threshold_values = command_line.HasOption("dump");
  bool do_performance_profiling = command_line.HasOption("profile");

  media::audio::test::FrequencySet::UseFullFrequencySet =
      (show_full_frequency_set || dump_threshold_values);

  testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();

  if (dump_threshold_values) {
    media::audio::test::AudioResult::DumpThresholdValues();
  }
  if (do_performance_profiling) {
    media::audio::test::AudioPerformance::Profile();
  }

  return result;
}
