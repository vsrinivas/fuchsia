// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "apps/mozart/src/launcher/launcher_app.h"
#include "lib/ftl/command_line.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  mtl::MessageLoop loop;

  std::unique_ptr<launcher::LauncherApp> app;
  loop.task_runner()->PostTask([&app, &command_line] {
    app = std::make_unique<launcher::LauncherApp>(command_line);
  });

  loop.Run();
  return 0;
}
