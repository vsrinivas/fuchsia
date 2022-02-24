// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_DRIVER_ECHO_CALL_ASYNC_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_DRIVER_ECHO_CALL_ASYNC_BENCHMARK_UTIL_H_

#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fdf/internal.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl_driver/cpp/wire_messaging.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <perftest/perftest.h>

namespace driver_benchmarks {

template <typename ProtocolType, typename FidlType>
class EchoServerAsyncImpl : public fdf::WireServer<ProtocolType> {
  void Echo(typename fdf::WireServer<ProtocolType>::EchoRequestView request, fdf::Arena& arena,
            typename fdf::WireServer<ProtocolType>::EchoCompleter::Sync& completer) override {
    completer.buffer(arena).Reply(std::move(request->val));
  }
};

template <typename ProtocolType, typename BuilderFunc>
bool EchoCallAsyncBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc, fidl::AnyArena&>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("EchoCall/WallTime");
  state->DeclareStep("Teardown/WallTime");

  void* driver = reinterpret_cast<void*>(uintptr_t(1));
  fdf_internal_push_driver(driver);
  auto deferred = fit::defer([]() { fdf_internal_pop_driver(); });

  auto dispatcher = fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED);
  ZX_ASSERT(ZX_OK == dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ZX_ASSERT(ZX_OK == channels.status_value());

  fdf::ServerEnd<ProtocolType> server_end(std::move(channels->end0));
  fdf::ClientEnd<ProtocolType> client_end(std::move(channels->end1));

  EchoServerAsyncImpl<ProtocolType, FidlType> server;
  fdf::BindServer(dispatcher->get(), std::move(server_end), &server);
  typename fdf::WireSharedClient<ProtocolType> client(std::move(client_end), dispatcher->get());

  while (state->KeepRunning()) {
    fidl::Arena<65536> fidl_arena;
    FidlType aligned_value = builder(fidl_arena);

    auto arena = fdf::Arena::Create(0, "");
    ZX_ASSERT(arena.is_ok());

    state->NextStep();  // End: Setup. Begin: EchoCall.

    sync::Completion completion;
    client.buffer(*arena)->Echo(
        std::move(aligned_value),
        [&state, &completion](fdf::WireUnownedResult<typename ProtocolType::Echo>& result) {
          state->NextStep();  // End: EchoCall. Begin: Teardown
          ZX_ASSERT(result.ok());
          completion.Signal();
        });

    completion.Wait();
  }

  return true;
}

}  // namespace driver_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_DRIVER_ECHO_CALL_ASYNC_BENCHMARK_UTIL_H_
