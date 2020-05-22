// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The cobalt system metrics collection daemon uses cobalt to log system metrics
// on a regular basis.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include <memory>

#include "src/cobalt/bin/system-metrics/system_metrics_daemon.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  // Parse the flags.
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line, {"cobalt", "system_metrics"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Create the SystemMetricsDaemon and start it.
  SystemMetricsDaemon daemon(loop.dispatcher(), context.get());
  FX_LOGS(INFO) << "System metrics daemon created.";
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher(), "system_metrics_daemon_provider");
  daemon.StartLogging();
  loop.Run();
  return 0;
}
