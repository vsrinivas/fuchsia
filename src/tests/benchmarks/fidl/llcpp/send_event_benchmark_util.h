// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LLCPP_SEND_EVENT_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LLCPP_SEND_EVENT_BENCHMARK_UTIL_H_

#include <lib/fidl/cpp/wire/arena.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <thread>
#include <type_traits>

#include <perftest/perftest.h>

namespace llcpp_benchmarks {

template <typename ProtocolType, typename BuilderFunc>
bool SendEventBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc, fidl::AnyArena&>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("SendEvent/WallTime");
  state->DeclareStep("Teardown/WallTime");

  auto endpoints = fidl::CreateEndpoints<ProtocolType>();
  ZX_ASSERT(endpoints.is_ok());

  class EventHandler : public fidl::WireSyncEventHandler<ProtocolType> {
   public:
    EventHandler(perftest::RepeatState* state, libsync::Completion& completion)
        : state_(state), completion_(completion) {}

    void Send(fidl::WireEvent<typename ProtocolType::Send>* event) override {
      state_->NextStep();  // End: SendEvent. Begin: Teardown.

      completion_.Signal();
    }

   private:
    perftest::RepeatState* state_;
    libsync::Completion& completion_;
  };

  libsync::Completion completion;

  std::thread receiver_thread([channel = std::move(endpoints->client), state, &completion]() {
    EventHandler event_handler(state, completion);
    while (event_handler.HandleOneEvent(channel.borrow()).ok()) {
    }
  });

  while (state->KeepRunning()) {
    fidl::Arena<65536> allocator;
    FidlType aligned_value = builder(allocator);

    state->NextStep();  // End: Setup. Begin: SendEvent.

    auto result = fidl::WireSendEvent(endpoints->server)->Send(std::move(aligned_value));
    ZX_ASSERT(result.ok());

    completion.Wait();
    completion.Reset();
  }

  // close the channel
  endpoints->server.reset();
  receiver_thread.join();

  return true;
}

}  // namespace llcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LLCPP_SEND_EVENT_BENCHMARK_UTIL_H_
