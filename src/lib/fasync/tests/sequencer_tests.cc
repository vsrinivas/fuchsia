// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/bridge.h>
#include <lib/fasync/sequencer.h>
#include <lib/fasync/single_threaded_executor.h>
#include <unistd.h>

#include <string>
#include <thread>

#include <zxtest/zxtest.h>

#undef assert
#define assert(expr)                                                             \
  do {                                                                           \
    if (!(expr)) {                                                               \
      std::cout << "Assert " << #expr << " failed at:" << __LINE__ << std::endl; \
      abort();                                                                   \
    }                                                                            \
  } while (false)

namespace {

TEST(SequencerTests, sequencing_tasks) {
  fasync::sequencer seq;
  std::string str;

  // This future writes ":a" sequentially then writes ":a2" later.
  auto a = fasync::make_future([&] { str += ":a"; }) | fasync::wrap_with(seq) |
           fasync::then([&] { str += ":a2"; });

  // This future writes ":b" sequentially then writes ":b2" and ":b3" later.
  // Also schedules another sequential task that writes ":e".
  auto b = fasync::make_future([&](fasync::context& context) {
             str += ":b";
             context.executor().schedule(fasync::make_future([&] { str += ":e"; }) |
                                         fasync::wrap_with(seq));
           }) |
           fasync::wrap_with(seq) |
           fasync::then(
               [&, count = 0](fasync::context& context) mutable -> fasync::try_poll<fit::failed> {
                 if (++count == 5) {
                   str += ":b3";
                   return fasync::ready(fit::failed());
                 }
                 str += ":b2";
                 context.suspend_task().resume();  // immediately resume
                 return fasync::pending();
               });

  static_assert(fasync::is_try_future_v<decltype(b)>);

  // This future writes ":c" sequentially then abandons itself.
  auto c = fasync::make_future([&](fasync::context& context) {
             str += ":c";
             context.suspend_task();  // abandon result
             return fasync::pending();
           }) |
           fasync::wrap_with(seq) | fasync::then([&] { str += ":c2"; });

  // This future writes ":d" sequentially.
  auto d = fasync::make_future([&] { str += ":d"; }) | fasync::wrap_with(seq);

  // These futures just write ":z1" and ":z2" whenever they happen to run.
  auto z1 = fasync::make_future([&] { str += ":z1"; });
  auto z2 = fasync::make_future([&] { str += ":z2"; });

  // Schedule the futures in an order which intentionally does not match the sequencing.
  fasync::single_threaded_executor executor;
  executor.schedule(std::move(z1));
  executor.schedule(std::move(b));
  executor.schedule(std::move(c));
  executor.schedule(std::move(a));
  executor.schedule(std::move(d));
  executor.schedule(std::move(z2));
  executor.run();

  // Evaluate the futures and check the execution order.
  EXPECT_STREQ(":z1:a:a2:z2:b:b2:c:b2:d:b2:e:b2:b3", str.c_str());
}

TEST(SequencerTests, thread_safety) {
  fasync::sequencer seq;
  fasync::single_threaded_executor executor;
  uint64_t run_count = 0;

  // Schedule work from a few threads, just to show that we can.
  constexpr int num_threads = 4;
  constexpr int num_tasks_per_thread = 100;
  std::thread threads[num_threads];
  for (int i = 0; i < num_threads; i++) {
    fasync::bridge<fit::failed> bridge;
    threads[i] = std::thread([&, completer = std::move(bridge.completer)]() mutable {
      for (int j = 0; j < num_tasks_per_thread; j++) {
        executor.schedule(fasync::make_future([&] { run_count++; }) | fasync::wrap_with(seq));
        usleep(1);
      }
      completer.complete_ok();
    });
    executor.schedule(bridge.consumer.future());
  }

  // Run the tasks.
  executor.run();
  for (int i = 0; i < num_threads; i++) {
    threads[i].join();
  }

  // We expect all tasks to have run.
  EXPECT_EQ(num_threads * num_tasks_per_thread, run_count);
}

}  // namespace
