// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "lib/syslog/cpp/log_settings.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/media/audio/audio_core/mixer/test/audio_performance.h"
#include "src/media/audio/audio_core/mixer/test/audio_result.h"
#include "src/media/audio/audio_core/mixer/test/frequency_set.h"
#include "src/media/audio/audio_core/mixer/test/mixer_tests_recap.h"
#include "src/media/audio/lib/logging/logging.h"

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (!fxl::SetTestSettings(command_line)) {
    return EXIT_FAILURE;
  }

  syslog::SetTags({"audio_core_mixer_test"});

  // --profile  Profile the performance of Mixer creation, Mix() and ProduceOutput() across numerous
  // configurations, and don't do further testing.
  //
  bool do_performance_profiling = command_line.HasOption("profile");
  if (do_performance_profiling) {
    media::audio::test::AudioPerformance::Profile();
    return 0;
  }

  // --full     Measure across the full frequency spectrum; display full results in tabular format.
  // --no-recap Do not display summary fidelity results.
  // --dump     Display full-spectrum results in importable format.
  //            (This flag is used when updating AudioResult kPrev arrays.)
  //
  bool show_full_frequency_set = command_line.HasOption("full");
  bool display_summary_results = !command_line.HasOption("no-recap");
  bool dump_threshold_values = command_line.HasOption("dump");

  media::audio::test::FrequencySet::UseFullFrequencySet =
      (show_full_frequency_set || dump_threshold_values);

  testing::InitGoogleTest(&argc, argv);

  int result = RUN_ALL_TESTS();

  if (display_summary_results) {
    media::audio::test::MixerTestsRecap::PrintFidelityResultsSummary();
  }
  if (dump_threshold_values) {
    media::audio::test::AudioResult::DumpThresholdValues();
  }

  return result;
}
