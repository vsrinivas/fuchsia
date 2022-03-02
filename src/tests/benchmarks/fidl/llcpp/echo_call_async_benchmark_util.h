// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ECHO_CALL_ASYNC_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ECHO_CALL_ASYNC_BENCHMARK_UTIL_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <perftest/perftest.h>

namespace llcpp_benchmarks {

template <typename ProtocolType, typename FidlType>
class EchoServerAsyncImpl : public fidl::WireServer<ProtocolType> {
  void Echo(typename fidl::WireServer<ProtocolType>::EchoRequestView request,
            typename fidl::WireServer<ProtocolType>::EchoCompleter::Sync& completer) override {
    completer.Reply(std::move(request->val));
  }
};

template <typename ProtocolType, typename BuilderFunc>
bool EchoCallAsyncBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc, fidl::AnyArena&>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("EchoCall/WallTime");
  state->DeclareStep("Teardown/WallTime");

  auto endpoints = fidl::CreateEndpoints<ProtocolType>();
  ZX_ASSERT(endpoints.is_ok());

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  EchoServerAsyncImpl<ProtocolType, FidlType> server;
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), &server);
  loop.StartThread();

  std::unique_ptr<fidl::WireClient<ProtocolType>> client =
      std::make_unique<fidl::WireClient<ProtocolType>>();
  sync::Completion bound;
  async::PostTask(loop.dispatcher(), [&]() {
    client->Bind(std::move(endpoints->client), loop.dispatcher());
    bound.Signal();
  });
  ZX_ASSERT(ZX_OK == bound.Wait());

  while (state->KeepRunning()) {
    fidl::Arena<65536> allocator;
    FidlType aligned_value = builder(allocator);

    sync::Completion completion;
    async::PostTask(loop.dispatcher(), [&]() {
      state->NextStep();  // End: Setup. Begin: EchoCall.

      (*client)->Echo(
          std::move(aligned_value),
          [&state, &completion](fidl::WireUnownedResult<typename ProtocolType::Echo>& result) {
            state->NextStep();  // End: EchoCall. Begin: Teardown
            ZX_ASSERT(result.ok());
            completion.Signal();
          });
    });
    ZX_ASSERT(ZX_OK == completion.Wait());
  }

  sync::Completion destroyed;
  async::PostTask(loop.dispatcher(),
                  [client = std::move(client), &destroyed]() { destroyed.Signal(); });
  ZX_ASSERT(ZX_OK == destroyed.Wait());

  loop.Quit();

  return true;
}

}  // namespace llcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ECHO_CALL_ASYNC_BENCHMARK_UTIL_H_
