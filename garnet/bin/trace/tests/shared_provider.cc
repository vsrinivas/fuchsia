// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper app for shared_provider_integration_tests.cc.
// This app generates a small number of known events that can then be
// tested for.

#include <lib/async-loop/cpp/loop.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>
#include <trace-provider/provider.h>

#include "garnet/bin/trace/tests/integration_test_utils.h"

const char kProviderName[] = "shared-provider-write-events";

namespace tracing {
namespace test {

static bool WriteEvents(async::Loop& loop) {
  trace::TraceProviderWithFdio provider{loop.dispatcher(), kProviderName};

  if (!WaitForTracingToStart(loop, kStartTimeout)) {
    FXL_LOG(ERROR) << "Timed out waiting for tracing to start";
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

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  if (!tracing::test::WriteEvents(loop)) {
    return EXIT_FAILURE;
  }

  // The provider is gone, but there can be a bit more work to do to cleanly
  // shut down trace-engine.
  zx_status_t status = loop.RunUntilIdle();
  if (status != ZX_OK && status != ZX_ERR_CANCELED) {
    FXL_LOG(ERROR) << "loop.Run() failed, status=" << status;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
