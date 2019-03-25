// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// We don't use gtest_main.cc so that we can process logging command line
// arguments.

#include <gtest/gtest.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>

int main(int argc, char **argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
