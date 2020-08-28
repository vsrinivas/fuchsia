// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LLCPP_SEND_EVENT_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LLCPP_SEND_EVENT_BENCHMARK_UTIL_H_

#include <lib/fidl/llcpp/coding.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <thread>
#include <type_traits>

#include <perftest/perftest.h>

namespace llcpp_benchmarks {

template <typename ProtocolType, typename BuilderFunc>
bool SendEventBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("SendEvent/WallTime");
  state->DeclareStep("Teardown/WallTime");

  zx::channel sender, receiver;
  zx_status_t status = zx::channel::create(0, &sender, &receiver);
  ZX_ASSERT(status == ZX_OK);

  bool ready = false;
  std::mutex mu;
  std::condition_variable cond;
  std::thread receiver_thread([channel = std::move(receiver), state, &ready, &mu, &cond]() {
    auto send = [state, &ready, &mu, &cond](typename ProtocolType::SendResponse* message) {
      state->NextStep();  // End: SendEvent. Begin: Teardown.
      {
        std::lock_guard<std::mutex> guard(mu);
        ready = true;
      }
      cond.notify_one();
      return ZX_OK;
    };
    typename ProtocolType::EventHandlers event_handlers{
        .send = send,
    };

    while (ProtocolType::Call::HandleEvents(channel.borrow(), event_handlers).ok())
      ;
  });

  while (state->KeepRunning()) {
    fidl::aligned<FidlType> aligned_value = builder();

    state->NextStep();  // End: Setup. Begin: SendEvent.

    ProtocolType::SendSendEvent(sender.borrow(), std::move(aligned_value.value));

    {
      std::unique_lock<std::mutex> lock(mu);
      while (!ready) {
        cond.wait(lock);
      }
      ready = false;
    }
  }

  // close the channel
  sender.reset();
  receiver_thread.join();

  return true;
}

}  // namespace llcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LLCPP_SEND_EVENT_BENCHMARK_UTIL_H_
