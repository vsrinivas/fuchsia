// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/bin/memory_monitor/monitor.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  FXL_VLOG(2) << argv[0] << ": starting";

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(
      loop.dispatcher(), memory::Monitor::kTraceName);

  memory::Monitor app(component::StartupContext::CreateFromStartupInfo(),
                      command_line,
                      loop.dispatcher());
  loop.Run();

  FXL_VLOG(2) << argv[0] << ": exiting";

  return 0;
}
