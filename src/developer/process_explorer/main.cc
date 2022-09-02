// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "src/developer/process_explorer/process_explorer.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line, {"process_explorer"}))
    return 1;

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto startup_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  process_explorer::Explorer app(std::move(startup_context));
  loop.Run();
}
