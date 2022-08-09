// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_DRIVER_LLCPP_ECHO_CALL_SYNC_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_DRIVER_LLCPP_ECHO_CALL_SYNC_BENCHMARK_UTIL_H_

#include <lib/async/cpp/task.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fdf/internal.h>
#include <lib/fidl_driver/cpp/wire_messaging.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <perftest/perftest.h>

namespace driver_benchmarks {

template <typename ProtocolType, typename FidlType>
class EchoServerSyncImpl : public fdf::WireServer<ProtocolType> {
  void Echo(typename fdf::WireServer<ProtocolType>::EchoRequestView request, fdf::Arena& arena,
            typename fdf::WireServer<ProtocolType>::EchoCompleter::Sync& completer) override {
    completer.buffer(arena).Reply(std::move(request->val));
  }
};

template <typename ProtocolType, typename BuilderFunc>
bool EchoCallSyncBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc, fidl::AnyArena&>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("EchoCall/WallTime");
  state->DeclareStep("Teardown/WallTime");

  void* driver = reinterpret_cast<void*>(uintptr_t(1));
  fdf_internal_push_driver(driver);
  auto deferred = fit::defer([]() { fdf_internal_pop_driver(); });

  libsync::Completion client_dispatcher_shutdown;
  auto client_dispatcher = fdf::Dispatcher::Create(
      FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "",
      [&](fdf_dispatcher_t* dispatcher) { client_dispatcher_shutdown.Signal(); });
  ZX_ASSERT(ZX_OK == client_dispatcher.status_value());

  libsync::Completion server_dispatcher_shutdown;
  auto server_dispatcher = fdf::Dispatcher::Create(
      FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "",
      [&](fdf_dispatcher_t* dispatcher) { server_dispatcher_shutdown.Signal(); });
  ZX_ASSERT(ZX_OK == server_dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ZX_ASSERT(ZX_OK == channels.status_value());

  fdf::ServerEnd<ProtocolType> server_end(std::move(channels->end0));
  fdf::ClientEnd<ProtocolType> client_end(std::move(channels->end1));

  EchoServerSyncImpl<ProtocolType, FidlType> server;
  fdf::ServerBindingRef binding_ref =
      fdf::BindServer(server_dispatcher->get(), std::move(server_end), &server);
  typename fdf::WireSyncClient<ProtocolType> client(std::move(client_end));

  sync_completion_t completion;
  auto run_on_dispatcher_thread = [&] {
    while (state->KeepRunning()) {
      fidl::Arena<65536> fidl_arena;
      FidlType aligned_value = builder(fidl_arena);

      auto arena = fdf::Arena::Create(0, 'BNCH');
      ZX_ASSERT(arena.is_ok());

      state->NextStep();  // End: Setup. Begin: EchoCall.

      auto result = client.buffer(*arena)->Echo(std::move(aligned_value));

      state->NextStep();  // End: EchoCall. Begin: Teardown

      ZX_ASSERT(result.ok());
    }

    // TODO(fxbug.dev/92489): If this call and wait is removed, the test will
    // flake by leaking |AsyncServerBinding| objects.
    binding_ref.Unbind();

    sync_completion_signal(&completion);
  };

  async::PostTask(client_dispatcher->async_dispatcher(), run_on_dispatcher_thread);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);

  client_dispatcher->ShutdownAsync();
  server_dispatcher->ShutdownAsync();
  ZX_ASSERT(ZX_OK == client_dispatcher_shutdown.Wait());
  ZX_ASSERT(ZX_OK == server_dispatcher_shutdown.Wait());

  return true;
}

}  // namespace driver_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_DRIVER_LLCPP_ECHO_CALL_SYNC_BENCHMARK_UTIL_H_
