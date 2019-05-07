// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/log_settings_command_line.h>

// Provide our own main so that --verbose,etc. are recognized.
int main(int argc, char** argv) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
