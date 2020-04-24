// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <stdlib.h>

#include "garnet/bin/trace/app.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  tracing::App app(context.get());
  int32_t return_code = EXIT_SUCCESS;
  async::PostTask(loop.dispatcher(), [&app, &command_line, &return_code, &loop] {
    app.Run(command_line, [&return_code, &loop](int32_t code) {
      return_code = code;
      loop.Quit();
    });
  });
  loop.Run();
  return return_code;
}
