// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/app.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  fsl::MessageLoop loop;
  auto context = app::ApplicationContext::CreateFromStartupInfo();

  tracing::App app(context.get());
  int32_t return_code = 0;
  loop.task_runner()->PostTask([&app, &command_line, &return_code, &loop] {
    app.Run(command_line, [&return_code, &loop](int32_t code) {
      return_code = code;
      loop.QuitNow();
    });
  });
  loop.Run();
  return return_code;
}
