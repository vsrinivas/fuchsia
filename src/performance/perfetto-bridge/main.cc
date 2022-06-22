// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/performance/perfetto-bridge/perfetto_bridge.h"

int main(int argc, char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  PerfettoBridge bridge;

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  fidl::BindingSet<fuchsia::tracing::perfetto::ProducerConnector> bridge_producer_bindings;
  context->outgoing()->AddPublicService(bridge_producer_bindings.GetHandler(&bridge));

  fidl::BindingSet<fuchsia::tracing::perfetto::ConsumerConnector> bridge_consumer_bindings;
  context->outgoing()->AddPublicService(bridge_producer_bindings.GetHandler(&bridge));

  FX_LOGS(INFO) << "PerfettoBridge starting.";
  return loop.Run();
}
