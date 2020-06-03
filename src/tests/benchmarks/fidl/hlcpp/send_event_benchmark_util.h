// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_HLCPP_SEND_EVENT_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_HLCPP_SEND_EVENT_BENCHMARK_UTIL_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/event_sender.h>

#include <thread>

namespace hlcpp_benchmarks {

template <typename ProtocolType, typename BuilderFunc>
bool SendEventBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  using FidlType = std::invoke_result_t<BuilderFunc>;

  state->DeclareStep("Setup/WallTime");
  state->DeclareStep("SendEvent/WallTime");
  state->DeclareStep("Teardown/WallTime");

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  fidl::InterfacePtr<ProtocolType> ptr;

  bool ready = false;
  std::mutex mu;
  std::condition_variable cond;
  ptr.events().Send = [state, &ready, &mu, &cond](FidlType) {
    state->NextStep();  // End: SendEvent. Begin: Teardown.
    {
      std::lock_guard<std::mutex> guard(mu);
      ready = true;
    }
    cond.notify_one();
  };
  loop.StartThread();

  auto request = ptr.NewRequest();
  fidl::EventSender<ProtocolType> sender(std::move(request));
  ZX_ASSERT(sender.channel().is_valid());
  while (state->KeepRunning()) {
    FidlType obj = builder();

    state->NextStep();  // End: Setup. Begin: SendEvent.

    sender.events().Send(std::move(obj));

    {
      std::unique_lock<std::mutex> lock(mu);
      while (!ready) {
        cond.wait(lock);
      }
      ready = false;
    }
  }

  loop.Quit();
  loop.JoinThreads();
  return true;
}

}  // namespace hlcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_HLCPP_SEND_EVENT_BENCHMARK_UTIL_H_
