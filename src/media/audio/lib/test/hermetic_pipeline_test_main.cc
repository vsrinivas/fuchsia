// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/media/audio/lib/test/hermetic_pipeline_test.h"

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);

  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(cl);
  media::audio::test::HermeticPipelineTest::save_input_and_output_files_ =
      cl.HasOption("save-inputs-and-outputs");

  auto result = RUN_ALL_TESTS();

  return result;
}
