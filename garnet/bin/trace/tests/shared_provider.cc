// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper app for shared_provider_integration_tests.cc.
// This app generates a small number of known events that can then be
// tested for.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

const char kSharedProviderWriteEventsProviderName[] = "shared-provider-write-events";

namespace tracing {
namespace test {

static bool WriteEvents(async::Loop& loop) {
  trace::TraceProviderWithFdio provider{loop.dispatcher(), kSharedProviderWriteEventsProviderName};

  if (!WaitForTracingToStart(loop, kStartTimeout)) {
    FX_LOGS(ERROR) << "Timed out waiting for tracing to start";
    return false;
  }

  WriteTestEvents(kNumSimpleTestEvents);

  return true;
}

}  // namespace test
}  // namespace tracing

int main(int argc, char** argv) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl)) {
    return EXIT_FAILURE;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  if (!tracing::test::WriteEvents(loop)) {
    return EXIT_FAILURE;
  }

  // The provider is gone, but there can be a bit more work to do to cleanly
  // shut down trace-engine.
  zx_status_t status = loop.RunUntilIdle();
  if (status != ZX_OK && status != ZX_ERR_CANCELED) {
    FX_LOGS(ERROR) << "loop.Run() failed, status=" << status;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
