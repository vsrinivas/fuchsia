// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The cobalt system metrics collection daemon uses cobalt to log system metrics
// on a regular basis.

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>

#include "garnet/bin/cobalt/system-metrics/system_metrics_daemon.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  std::unique_ptr<component::StartupContext> context(
      component::StartupContext::CreateFromStartupInfo());

  // Create the SystemMetricsDaemon and start it.
  SystemMetricsDaemon daemon(loop.dispatcher(), context.get());
  daemon.Work();
  loop.Run();
  return 0;
}
