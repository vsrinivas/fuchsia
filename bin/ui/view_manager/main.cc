// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/bin/ui/view_manager/view_manager_app.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  view_manager::ViewManagerApp app;

  loop.Run();
  return 0;
}
