// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include "garnet/bin/cpuperf_provider/app.h"
#include "garnet/lib/perfmon/controller.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  if (!perfmon::Controller::IsSupported()) {
    FX_LOGS(INFO) << "Exiting, perfmon device not supported";
    return 0;
  }

  FX_VLOGS(2) << argv[0] << ": starting";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher(), "cpuperf_provider");

  cpuperf_provider::App app(command_line);
  loop.Run();

  FX_VLOGS(2) << argv[0] << ": exiting";

  return 0;
}
