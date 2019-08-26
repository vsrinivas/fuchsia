// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper app for provider_destruction_tests.cc.

#include <lib/async/cpp/task.h>
#include <lib/async-loop/cpp/loop.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>
#include <trace-provider/provider.h>

#include "garnet/bin/trace/tests/integration_test_utils.h"

const char kProviderName[] = "provider-destruction";

static bool WriteEvents(async::Loop& loop) {
  fbl::unique_ptr<trace::TraceProviderWithFdio> provider;
  bool already_started;
  if (!CreateProviderSynchronously(loop, kProviderName, &provider, &already_started)) {
    return false;
  }

  // The program may not be being run under tracing. If it is tracing should have already started.
  // Things are a little different because the provider loop is running in the background.
  if (already_started) {
    // At this point we're registered with trace-manager, and we know tracing
    // has started. But we haven't received the Start() request yet, which
    // contains the trace buffer (as a vmo) and other things. So wait for it.
    async::Loop wait_loop(&kAsyncLoopConfigNoAttachToThread);
    if (!WaitForTracingToStart(wait_loop, kStartTimeout)) {
      FXL_LOG(ERROR) << "Provider " << kProviderName << " failed waiting for tracing to start";
      return false;
    }
  }

  WriteTestEvents(kNumSimpleTestEvents);

  return true;
}

int main(int argc, char** argv) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl)) {
    return EXIT_FAILURE;
  }

  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  // Run the loop in the background so that we can trigger races between provider destruction
  // and servicing of requests from trace-manager.
  loop.StartThread();

  if (!WriteEvents(loop)) {
    return EXIT_FAILURE;
  }

  loop.Quit();
  loop.JoinThreads();

  return EXIT_SUCCESS;
}
