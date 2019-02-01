// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>

#include "gtest/gtest.h"

static void SetVerbosityFromArgv(int argc, char** argv) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(cl);
}

int main(int argc, char** argv) {
  SetVerbosityFromArgv(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
