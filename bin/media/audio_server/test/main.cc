// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/audio_result.h"
#include "garnet/bin/media/audio_server/test/frequency_set.h"
#include "gtest/gtest.h"
#include "lib/fxl/command_line.h"

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  // --full  Display results for the full frequency spectrum.
  // --dump  Display results in importable format.
  //         This flag is used when updating AudioResult kPrev arrays.
  bool show_full_frequency_set = command_line.HasOption("full");
  bool dump_threshold_values = command_line.HasOption("dump");

  media::audio::test::FrequencySet::UseFullFrequencySet =
      (show_full_frequency_set || dump_threshold_values);

  testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();

  if (dump_threshold_values) {
    media::audio::test::AudioResult::DumpThresholdValues();
  }

  return result;
}
