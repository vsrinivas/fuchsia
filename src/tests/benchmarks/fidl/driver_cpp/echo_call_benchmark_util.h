// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_DRIVER_CPP_ECHO_CALL_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_DRIVER_CPP_ECHO_CALL_BENCHMARK_UTIL_H_

#include <lib/async/cpp/task.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fdf/testing.h>
#include <lib/fidl_driver/cpp/natural_messaging.h>
#include <lib/fidl_driver/cpp/wire_messaging.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <perftest/perftest.h>

namespace driver_benchmarks {

template <typename ProtocolType, typename FidlType>
class EchoServerImpl : public fdf::Server<ProtocolType> {
  void Echo(typename fdf::Server<ProtocolType>::EchoRequest& request,
            typename fdf::Server<ProtocolType>::EchoCompleter::Sync& completer) override {
    completer.Reply(std::move(request.val()));
  }
};

template <typename ProtocolType, typename BuilderFunc>
bool EchoCallBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("EchoCall/WallTime");
  state->DeclareStep("Teardown/WallTime");

  void* driver = reinterpret_cast<void*>(1);
  fdf_testing_push_driver(driver);
  auto deferred = fit::defer([]() { fdf_testing_pop_driver(); });

  libsync::Completion dispatcher_shutdown;
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ZX_ASSERT(ZX_OK == dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ZX_ASSERT(ZX_OK == channels.status_value());

  fdf::ServerEnd<ProtocolType> server_end(std::move(channels->end0));
  fdf::ClientEnd<ProtocolType> client_end(std::move(channels->end1));

  EchoServerImpl<ProtocolType, FidlType> server;
  fdf::BindServer(dispatcher->get(), std::move(server_end), &server);

  fdf::Client<ProtocolType> client;
  libsync::Completion bound;
  async::PostTask(dispatcher->async_dispatcher(), [&]() {
    client.Bind(std::move(client_end), dispatcher->get());
    bound.Signal();
  });
  ZX_ASSERT(ZX_OK == bound.Wait());

  while (state->KeepRunning()) {
    libsync::Completion completion;
    async::PostTask(dispatcher->async_dispatcher(), [&]() {
      FidlType aligned_value = builder();

      state->NextStep();  // End: Setup. Begin: EchoCall.

      client->Echo(std::move(aligned_value))
          .ThenExactlyOnce([&state, &completion](fdf::Result<typename ProtocolType::Echo>& result) {
            state->NextStep();  // End: EchoCall. Begin: Teardown
            ZX_ASSERT(result.is_ok());
            completion.Signal();
          });
    });
    ZX_ASSERT(ZX_OK == completion.Wait());
  }

  libsync::Completion destroyed;
  async::PostTask(dispatcher->async_dispatcher(),
                  [client = std::move(client), &destroyed]() { destroyed.Signal(); });
  ZX_ASSERT(ZX_OK == destroyed.Wait());

  dispatcher->ShutdownAsync();
  ZX_ASSERT(ZX_OK == dispatcher_shutdown.Wait());

  return true;
}

}  // namespace driver_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_DRIVER_CPP_ECHO_CALL_BENCHMARK_UTIL_H_
