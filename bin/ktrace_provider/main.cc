// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/bin/ktrace_provider/app.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

using namespace ktrace_provider;

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trace::TraceProvider trace_provider(loop.dispatcher());

  App app(command_line);
  loop.Run();
  return 0;
}
