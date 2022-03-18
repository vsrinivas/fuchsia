// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_CPP_SEND_EVENT_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_CPP_SEND_EVENT_BENCHMARK_UTIL_H_

#include <lib/fidl/cpp/channel.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <thread>
#include <type_traits>

#include <perftest/perftest.h>

namespace cpp_benchmarks {

template <typename ProtocolType, typename BuilderFunc>
bool SendEventBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("SendEvent/WallTime");
  state->DeclareStep("Teardown/WallTime");

  auto endpoints = fidl::CreateEndpoints<ProtocolType>();
  ZX_ASSERT(endpoints.is_ok());

  class EventHandler : public fidl::AsyncEventHandler<ProtocolType> {
   public:
    EventHandler(perftest::RepeatState* state) : state_(state) {}

    void Send(fidl::Event<typename ProtocolType::Send>& event) override {
      state_->NextStep();  // End: SendEvent. Begin: Teardown.

      completion_.Signal();
    }

    libsync::Completion& completion() { return completion_; }

   private:
    perftest::RepeatState* state_;
    libsync::Completion completion_;
  };

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread();

  EventHandler event_handler(state);
  fidl::Client<ProtocolType> client;
  libsync::Completion bound;
  async::PostTask(loop.dispatcher(), [&]() {
    client.Bind(std::move(endpoints->client), loop.dispatcher(), &event_handler);
    bound.Signal();
  });
  ZX_ASSERT(ZX_OK == bound.Wait());

  while (state->KeepRunning()) {
    FidlType aligned_value = builder();

    state->NextStep();  // End: Setup. Begin: SendEvent.

    auto result = fidl::SendEvent(std::move(endpoints->server))->Send(std::move(aligned_value));
    ZX_ASSERT(result.is_ok());

    event_handler.completion().Wait();
    event_handler.completion().Reset();
  }

  libsync::Completion destroyed;
  async::PostTask(loop.dispatcher(),
                  [client = std::move(client), &destroyed]() { destroyed.Signal(); });
  ZX_ASSERT(ZX_OK == destroyed.Wait());

  return true;
}

}  // namespace cpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_CPP_SEND_EVENT_BENCHMARK_UTIL_H_
