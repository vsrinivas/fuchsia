// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;
  auto context = faux::ApplicationContext::CreateFromStartupInfo();

  tracing::App app(context.get());
  app.Run(command_line);
  loop.Run();
  return 0;
}
