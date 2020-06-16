// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/media/audio/drivers/test/test_base.h"
#include "src/media/audio/lib/logging/logging.h"

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (!fxl::SetTestSettings(command_line)) {
    return EXIT_FAILURE;
  }

  syslog::SetTags({"audio_driver_test"});

  // --admin   Validate driver commands that require the privileged channel, such as SetFormat.
  //
  media::audio::drivers::test::TestBase::test_admin_functions_ = command_line.HasOption("admin");

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
