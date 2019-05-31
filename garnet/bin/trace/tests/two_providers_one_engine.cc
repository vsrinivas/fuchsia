// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Create two providers that don't do anything.
// The test is to exercise graceful handling when a process contains
// two providers.

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>
#include <stdlib.h>
#include <trace-provider/provider.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

int main(int argc, char* argv[]) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  // Create these synchronously as we don't want the test to start
  // until after we've completed registration with trace-manager.
  // Run the loop in this thread to reduce timing differences that
  // make debugging harder.
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  loop.StartThread("provider-thread", nullptr);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  std::unique_ptr<trace::TraceProvider> provider1;
  bool already_started;
  if (!trace::TraceProvider::CreateSynchronously(
          dispatcher, "provider1", &provider1, &already_started)) {
    FXL_LOG(ERROR) << "Failed to create provider1";
    return EXIT_FAILURE;
  }

  std::unique_ptr<trace::TraceProvider> provider2;
  if (!trace::TraceProvider::CreateSynchronously(
          dispatcher, "provider2", &provider2, &already_started)) {
    FXL_LOG(ERROR) << "Failed to create provider2";
    return EXIT_FAILURE;
  }

  // Notify the harness that we're up and running.
  // PA_USER0 is an event pair passed to us by the test harness.
  zx::eventpair event{zx_take_startup_handle(PA_HND(PA_USER0, 0))};
  auto status = event.signal_peer(0u, ZX_EVENTPAIR_SIGNALED);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Signaling event pair failed: "
                   << zx_status_get_string(status);
    return EXIT_FAILURE;
  }

  // The test harness will signal us it's done by closing its side
  // of the eventpair.
  async::Wait wait{
      event.get(), ZX_EVENTPAIR_PEER_CLOSED,
      [&loop](async_dispatcher_t* dispatcher, async::Wait* wait,
              zx_status_t status, const zx_packet_signal_t* signal) {
        if (signal->observed & ZX_EVENTPAIR_PEER_CLOSED) {
          loop.Quit();
        }
      }};
  wait.Begin(dispatcher);
  loop.Run();

  return EXIT_SUCCESS;
}
