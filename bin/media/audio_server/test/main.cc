// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/frequency_set.h"
#include "gtest/gtest.h"
#include "lib/fxl/command_line.h"

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  // --full
  if (command_line.HasOption("full")) {
    media::audio::test::FrequencySet::UseFullFrequencySet = true;
    std::cout << "\nProfile flag was set to 'full'";
  } else {
    media::audio::test::FrequencySet::UseFullFrequencySet = false;
    printf("\nProfile flag was not set\n\n");
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
