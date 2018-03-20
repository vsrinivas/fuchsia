// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/loop.h>

#include "garnet/bin/sysmgr/app.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  async_loop_config_t config = {
      .make_default_for_current_thread = true,
  };
  async::Loop loop(&config);
  sysmgr::App app;
  loop.Run();
  return 0;
}
