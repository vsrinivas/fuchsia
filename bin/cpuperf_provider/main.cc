// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>
#include <lib/async-loop/cpp/loop.h>

#ifdef __x86_64__
#include "garnet/bin/cpuperf_provider/app.h"
#endif

#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
#ifdef __x86_64__
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  FXL_VLOG(2) << argv[0] << ": starting";

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());

  cpuperf_provider::App app(command_line);
  loop.Run();

  FXL_VLOG(2) << argv[0] << ": exiting";
#else
  FXL_LOG(INFO) << "cpuperf_provider: unsupported architecture";
#endif

  return 0;
}
