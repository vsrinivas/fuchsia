// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "garnet/bin/trace/app.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();

  tracing::App app(context.get());
  int32_t return_code = 0;
  async::PostTask(loop.dispatcher(), [&app, &command_line, &return_code, &loop] {
    app.Run(command_line, [&return_code, &loop](int32_t code) {
      return_code = code;
      loop.Quit();
    });
  });
  loop.Run();
  return return_code;
}
