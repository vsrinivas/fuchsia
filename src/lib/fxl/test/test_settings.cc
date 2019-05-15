// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "test_settings.h"

#include <stdlib.h>

#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"

namespace fxl {

bool SetTestSettings(const CommandLine& command_line) {
  std::string random_seed;
  if (command_line.GetOptionValue("test_loop_seed", &random_seed)) {
    setenv("TEST_LOOP_RANDOM_SEED", random_seed.c_str(), /*overwrite=*/true);
  }
  return fxl::SetLogSettingsFromCommandLine(command_line);
}

bool SetTestSettings(int argc, const char* const* argv) {
  return SetTestSettings(fxl::CommandLineFromArgcArgv(argc, argv));
}

}  // namespace fxl
