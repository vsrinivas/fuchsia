// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_HLCPP_ECHO_CALL_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_HLCPP_ECHO_CALL_BENCHMARK_UTIL_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>

#include <perftest/perftest.h>

namespace hlcpp_benchmarks {

template <typename ProtocolType, typename FidlType>
class EchoServerImpl : public ProtocolType {
  void Echo(FidlType val, typename ProtocolType::EchoCallback callback) override {
    callback(std::move(val));
  }
};

template <typename ProtocolType, typename BuilderFunc>
bool EchoCallBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("EchoCall/WallTime");
  state->DeclareStep("Teardown/WallTime");

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  fidl::SynchronousInterfacePtr<ProtocolType> ptr;

  EchoServerImpl<ProtocolType, FidlType> server;
  fidl::Binding<ProtocolType> server_binding(&server);
  server_binding.Bind(ptr.NewRequest());

  loop.StartThread();

  while (state->KeepRunning()) {
    FidlType in = builder();

    state->NextStep();  // End: Setup. Begin: EchoCall.

    FidlType out;
    zx_status_t status = ptr->Echo(std::move(in), &out);

    state->NextStep();  // End: EchoCall. Begin: Teardown

    ZX_ASSERT(status == ZX_OK);
  }

  loop.Quit();
  return true;
}

}  // namespace hlcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_HLCPP_ECHO_CALL_BENCHMARK_UTIL_H_
