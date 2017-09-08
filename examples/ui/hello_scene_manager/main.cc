// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/hello_scene_manager/app.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;
  hello_scene_manager::App app;
  loop.task_runner()->PostDelayedTask(
      [&loop] {
        FTL_LOG(INFO) << "Quitting.";
        loop.QuitNow();
      },
      ftl::TimeDelta::FromSeconds(50));
  loop.Run();
  return 0;
}
