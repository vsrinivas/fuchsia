// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/zircon/benchmarks/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>

#include <functional>
#include <thread>
#include <vector>

#include "assert.h"
#include "test_runner.h"

// This file benchmarks a common FIDL server using a libasync dispatch loop.
//
// In each benchmark iteration, a single thread enqueues N messages (the
// "client write" phase), and then runs an async loop to idle ("server
// process" phase), where each message will be handled by a null callback.
//
// The server process phase exercises Zircon's channel waiting and reading
// mechanisms, trivial FIDL message decoding, and the libasync dispatch loop.

namespace {

class NotificationImpl : public fuchsia::zircon::benchmarks::Notification {
 public:
  void Notify() override {}
};

bool AsyncLoopProcessBatch(uint32_t count, perftest::RepeatState* state) {
  state->DeclareStep("client_write");
  state->DeclareStep("server_process");

  // Set up client.
  zx::channel server, client;
  ASSERT_OK(zx::channel::create(0, &server, &client));
  fuchsia::zircon::benchmarks::NotificationSyncPtr service;
  service.Bind(std::move(client));

  // Set up server.
  NotificationImpl service_impl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  fidl::Binding<fuchsia::zircon::benchmarks::Notification> binding(&service_impl,
                                                                   std::move(server));
  binding.set_error_handler([&loop](zx_status_t status) { loop.Quit(); });

  // Start the benchmark.
  while (state->KeepRunning()) {
    // Enqueue 'count' messages.
    for (uint32_t i = 0; i < count; i++) {
      ASSERT_OK(service->Notify());
    }

    state->NextStep();

    // Process all messages.'
    loop.RunUntilIdle();
  }

  return true;
}

void RegisterTests() {
  // Return a benchmark function the processes "count" messages per batch.
  auto AsyncLoopProcessBatchN = [](uint32_t count) {
    return [count](perftest::RepeatState* state) { return AsyncLoopProcessBatch(count, state); };
  };

  perftest::RegisterTest("AsyncLoopProcessBatch/1", AsyncLoopProcessBatchN(1));
  perftest::RegisterTest("AsyncLoopProcessBatch/2", AsyncLoopProcessBatchN(2));
  perftest::RegisterTest("AsyncLoopProcessBatch/4", AsyncLoopProcessBatchN(4));
  perftest::RegisterTest("AsyncLoopProcessBatch/8", AsyncLoopProcessBatchN(8));
  perftest::RegisterTest("AsyncLoopProcessBatch/16", AsyncLoopProcessBatchN(16));
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
