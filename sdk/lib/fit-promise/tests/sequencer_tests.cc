// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/bridge.h>
#include <lib/fpromise/sequencer.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <unistd.h>

#include <string>
#include <thread>

#include <zxtest/zxtest.h>

namespace {

TEST(SequencerTests, sequencing_tasks) {
  fpromise::sequencer seq;
  std::string str;

  // This promise writes ":a" sequentially then writes ":a2" later.
  auto a = fpromise::make_promise([&] { str += ":a"; })
               .wrap_with(seq)
               .then([&](const fpromise::result<>&) { str += ":a2"; });

  // This promise writes ":b" sequentially then writes ":b2" and ":b3" later.
  // Also schedules another sequential task that writes ":e".
  auto b = fpromise::make_promise([&](fpromise::context& context) {
             str += ":b";
             context.executor()->schedule_task(
                 fpromise::make_promise([&] { str += ":e"; }).wrap_with(seq));
           })
               .wrap_with(seq)
               .then([&, count = 0](fpromise::context& context,
                                    const fpromise::result<>&) mutable -> fpromise::result<> {
                 if (++count == 5) {
                   str += ":b3";
                   return fpromise::error();
                 }
                 str += ":b2";
                 context.suspend_task().resume_task();  // immediately resume
                 return fpromise::pending();
               });

  // This promise writes ":c" sequentially then abandons itself.
  auto c = fpromise::make_promise([&](fpromise::context& context) {
             str += ":c";
             context.suspend_task();  // abandon result
             return fpromise::pending();
           })
               .wrap_with(seq)
               .then([&](const fpromise::result<>&) { str += ":c2"; });

  // This promise writes ":d" sequentially.
  auto d = fpromise::make_promise([&] { str += ":d"; }).wrap_with(seq);

  // These promises just write ":z1" and ":z2" whenever they happen to run.
  auto z1 = fpromise::make_promise([&] { str += ":z1"; });
  auto z2 = fpromise::make_promise([&] { str += ":z2"; });

  // Schedule the promises in an order which intentionally does not
  // match the sequencing.
  fpromise::single_threaded_executor executor;
  executor.schedule_task(std::move(z1));
  executor.schedule_task(std::move(b));
  executor.schedule_task(std::move(c));
  executor.schedule_task(std::move(a));
  executor.schedule_task(std::move(d));
  executor.schedule_task(std::move(z2));
  executor.run();

  // Evaluate the promises and check the execution order.
  EXPECT_STREQ(":z1:a:a2:z2:b:b2:c:b2:d:b2:e:b2:b3", str.c_str());
}

TEST(SequencerTests, thread_safety) {
  fpromise::sequencer seq;
  fpromise::single_threaded_executor executor;
  uint64_t run_count = 0;

  // Schedule work from a few threads, just to show that we can.
  constexpr int num_threads = 4;
  constexpr int num_tasks_per_thread = 100;
  std::thread threads[num_threads];
  for (int i = 0; i < num_threads; i++) {
    fpromise::bridge bridge;
    threads[i] = std::thread([&, completer = std::move(bridge.completer)]() mutable {
      for (int j = 0; j < num_tasks_per_thread; j++) {
        executor.schedule_task(fpromise::make_promise([&] { run_count++; }).wrap_with(seq));
        usleep(1);
      }
      completer.complete_ok();
    });
    executor.schedule_task(bridge.consumer.promise());
  }

  // Run the tasks.
  executor.run();
  for (int i = 0; i < num_threads; i++)
    threads[i].join();

  // We expect all tasks to have run.
  EXPECT_EQ(num_threads * num_tasks_per_thread, run_count);
}

}  // namespace
