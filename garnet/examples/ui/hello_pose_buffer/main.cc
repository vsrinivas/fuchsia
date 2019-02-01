// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <zx/time.h>

#include "garnet/examples/ui/hello_pose_buffer/app.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  hello_pose_buffer::App app(&loop);
  async::PostDelayedTask(loop.dispatcher(),
                         [&loop] {
                           FXL_LOG(INFO) << "Quitting.";
                           loop.Quit();
                         },
                         zx::sec(50));
  loop.Run();
  return 0;
}
