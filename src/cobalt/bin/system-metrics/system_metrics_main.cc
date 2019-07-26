// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The cobalt system metrics collection daemon uses cobalt to log system metrics
// on a regular basis.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/syslogger/init.h>
#include <lib/sys/cpp/component_context.h>
#include <trace-provider/provider.h>

#include <memory>

#include "lib/syslog/cpp/logger.h"
#include "src/cobalt/bin/system-metrics/system_metrics_daemon.h"

int main(int argc, const char** argv) {
  // Parse the flags.
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fsl::InitLoggerFromCommandLine(command_line, {"cobalt", "system_metrics"});

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();

  // Create the SystemMetricsDaemon and start it.
  SystemMetricsDaemon daemon(loop.dispatcher(), context.get());
  FX_LOGS(INFO) << "System metrics daemon created.";
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher(), "system_metrics_daemon_provider");
  daemon.StartLogging();
  loop.Run();
  return 0;
}
