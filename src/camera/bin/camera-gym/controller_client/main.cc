// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
//#include <lib/syslog/cpp/macros.h>

#include "controller_client_app.h"
#include "src/camera/bin/camera-gym/controller_parser/controller_parser.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // Break down command arguments list into individual commands.
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  // Parse each command into command and associated parameters.
  camera::ControllerParser controller_parser;
  auto result = controller_parser.ParseArgcArgv(argc, argv);
  if (result.is_error()) {
    fprintf(stderr, "ERROR: Command not understood\n");
    return -1;
  }
  auto commands = result.take_value();

  // Start and run the application on the list of commands.
  camera::ControllerClientApp app(loop);
  if (commands.size() < 1) {
    fprintf(stderr, "ERROR: No commands\n");
    return -1;
  }
  app.Start(std::move(commands));
  loop.Run();
  return 0;
}
