// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ECHO_CALL_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ECHO_CALL_BENCHMARK_UTIL_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/coding.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <perftest/perftest.h>

namespace llcpp_benchmarks {

template <typename ProtocolType, typename FidlType>
class EchoServerImpl : public ProtocolType::Interface {
  void Echo(FidlType val,
            typename ProtocolType::Interface::EchoCompleter::Sync completer) override {
    completer.Reply(std::move(val));
  }
};

template <typename ProtocolType, typename BuilderFunc>
bool EchoCallBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("EchoCall/WallTime");
  state->DeclareStep("Teardown/WallTime");

  zx::channel client_end, server_end;
  ZX_ASSERT(ZX_OK == zx::channel::create(0, &client_end, &server_end));

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  EchoServerImpl<ProtocolType, FidlType> server;
  fidl::BindServer(loop.dispatcher(), std::move(server_end), &server);
  loop.StartThread();
  typename ProtocolType::SyncClient client(std::move(client_end));

  while (state->KeepRunning()) {
    fidl::aligned<FidlType> aligned_value = builder();

    state->NextStep();  // End: Setup. Begin: EchoCall.

    auto result = client.Echo(std::move(aligned_value.value));

    state->NextStep();  // End: EchoCall. Begin: Teardown

    ZX_ASSERT(result.ok());
  }

  loop.Quit();

  return true;
}

}  // namespace llcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ECHO_CALL_BENCHMARK_UTIL_H_
