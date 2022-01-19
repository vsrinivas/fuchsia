// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <lib/zx/event.h>

#include <future>

#include <zxtest/zxtest.h>

namespace {

TEST(ExecutorTests, running_tasks) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());
  uint64_t run_count[3] = {};

  // Schedule a task that runs once and increments a counter.
  executor.schedule_task(fpromise::make_promise([&] { run_count[0]++; }));

  // Schedule a task that runs once, increments a counter,
  // and scheduled another task.
  executor.schedule_task(fpromise::make_promise([&](fpromise::context& context) {
    run_count[1]++;
    assert(context.executor() == &executor);
    context.executor()->schedule_task(fpromise::make_promise([&] { run_count[2]++; }));
  }));
  EXPECT_EQ(0, run_count[0]);
  EXPECT_EQ(0, run_count[1]);
  EXPECT_EQ(0, run_count[2]);

  // We expect that all of the tasks will run to completion including newly
  // scheduled tasks.
  loop.RunUntilIdle();
  EXPECT_EQ(1, run_count[0]);
  EXPECT_EQ(1, run_count[1]);
  EXPECT_EQ(1, run_count[2]);
}

TEST(ExecutorTests, suspending_and_resuming_tasks) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  uint64_t run_count[5] = {};
  uint64_t resume_count[5] = {};
  uint64_t resume_count4b = 0;

  // Schedule a task that suspends itself and immediately resumes.
  executor.schedule_task(
      fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<> {
        if (++run_count[0] == 100)
          return fpromise::ok();
        resume_count[0]++;
        context.suspend_task().resume_task();
        return fpromise::pending();
      }));

  // Schedule a task that requires several iterations to complete, each
  // time scheduling another task to resume itself after suspension.
  executor.schedule_task(
      fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<> {
        if (++run_count[1] == 100)
          return fpromise::ok();
        context.executor()->schedule_task(
            fpromise::make_promise([&, s = context.suspend_task()]() mutable {
              resume_count[1]++;
              s.resume_task();
            }));
        return fpromise::pending();
      }));

  // Same as the above but use another thread to resume.
  executor.schedule_task(
      fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<> {
        if (++run_count[2] == 100)
          return fpromise::ok();
        [[maybe_unused]] auto a =
            std::async(std::launch::async, [&, s = context.suspend_task()]() mutable {
              resume_count[2]++;
              s.resume_task();
            });
        return fpromise::pending();
      }));

  // Schedule a task that suspends itself but doesn't actually return pending
  // so it only runs once.
  executor.schedule_task(
      fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<> {
        run_count[3]++;
        context.suspend_task();
        return fpromise::ok();
      }));

  // Schedule a task that suspends itself and arranges to be resumed on
  // one of two other threads, whichever gets there first.
  executor.schedule_task(
      fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<> {
        if (++run_count[4] == 100)
          return fpromise::ok();
        [[maybe_unused]] auto a =
            std::async(std::launch::async, [&, s = context.suspend_task()]() mutable {
              resume_count[4]++;
              s.resume_task();
            });
        [[maybe_unused]] auto b =
            std::async(std::launch::async, [&, s = context.suspend_task()]() mutable {
              resume_count4b++;  // use a different variable to avoid data races
              s.resume_task();
            });
        return fpromise::pending();
      }));

  // We expect the tasks to have been completed after being resumed several times.
  loop.RunUntilIdle();
  EXPECT_EQ(100, run_count[0]);
  EXPECT_EQ(99, resume_count[0]);
  EXPECT_EQ(100, run_count[1]);
  EXPECT_EQ(99, resume_count[1]);
  EXPECT_EQ(100, run_count[2]);
  EXPECT_EQ(99, resume_count[2]);
  EXPECT_EQ(1, run_count[3]);
  EXPECT_EQ(0, resume_count[3]);
  EXPECT_EQ(100, run_count[4]);
  EXPECT_EQ(99, resume_count[4]);
  EXPECT_EQ(99, resume_count4b);
}

TEST(ExecutorTests, abandoning_tasks) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());
  uint64_t run_count[4] = {};
  uint64_t destruction[4] = {};

  // Schedule a task that returns pending without suspending itself
  // so it is immediately abandoned.
  executor.schedule_task(fpromise::make_promise(
      [&, d = fit::defer([&] { destruction[0]++; })]() -> fpromise::result<> {
        run_count[0]++;
        return fpromise::pending();
      }));

  // Schedule a task that suspends itself but drops the |suspended_task|
  // object before returning so it is immediately abandoned.
  executor.schedule_task(
      fpromise::make_promise([&, d = fit::defer([&] { destruction[1]++; })](
                                 fpromise::context& context) -> fpromise::result<> {
        run_count[1]++;
        context.suspend_task();  // ignore result
        return fpromise::pending();
      }));

  // Schedule a task that suspends itself and drops the |suspended_task|
  // object from a different thread so it is abandoned concurrently.
  executor.schedule_task(
      fpromise::make_promise([&, d = fit::defer([&] { destruction[2]++; })](
                                 fpromise::context& context) -> fpromise::result<> {
        run_count[2]++;
        [[maybe_unused]] auto a = std::async(std::launch::async, [s = context.suspend_task()] {});
        return fpromise::pending();
      }));

  // Schedule a task that creates several suspended task handles and drops
  // them all on the floor.
  executor.schedule_task(
      fpromise::make_promise([&, d = fit::defer([&] { destruction[3]++; })](
                                 fpromise::context& context) -> fpromise::result<> {
        run_count[3]++;
        fpromise::suspended_task s[3];
        for (size_t i = 0; i < 3; i++)
          s[i] = context.suspend_task();
        return fpromise::pending();
      }));

  // We expect the tasks to have been executed but to have been abandoned.
  loop.RunUntilIdle();
  EXPECT_EQ(1, run_count[0]);
  EXPECT_EQ(1, destruction[0]);
  EXPECT_EQ(1, run_count[1]);
  EXPECT_EQ(1, destruction[1]);
  EXPECT_EQ(1, run_count[2]);
  EXPECT_EQ(1, destruction[2]);
  EXPECT_EQ(1, run_count[3]);
  EXPECT_EQ(1, destruction[3]);
}

TEST(ExecutorTests, dispatcher_property) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());
  EXPECT_EQ(loop.dispatcher(), executor.dispatcher());

  // Just check that the task receives a context that exposes the dispatcher
  // property.
  async_dispatcher_t* received_dispatcher = nullptr;
  executor.schedule_task(fpromise::make_promise([&](fpromise::context& context) {
    received_dispatcher = context.as<async::Context>().dispatcher();
  }));
  EXPECT_NULL(received_dispatcher);

  // We expect that all of the tasks will run to completion.
  loop.RunUntilIdle();
  EXPECT_EQ(loop.dispatcher(), received_dispatcher);
}

TEST(ExecutorTests, tasks_scheduled_after_loop_shutdown_are_immediately_destroyed) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  // Shutdown the loop then schedule a task.
  // The task should be immediately destroyed.
  loop.Shutdown();
  bool was_destroyed = false;
  executor.schedule_task(
      fpromise::make_promise([d = fit::defer([&] { was_destroyed = true; })] {}));
  EXPECT_TRUE(was_destroyed);
}

TEST(ExecutorTests, when_loop_is_shutdown_all_remaining_tasks_are_immediately_destroyed) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  // Schedule a task and let it be suspended.
  fpromise::suspended_task suspend;
  bool was_destroyed[2] = {};
  executor.schedule_task(fpromise::make_promise(
      [&, d = fit::defer([&] { was_destroyed[0] = true; })](fpromise::context& context) {
        suspend = context.suspend_task();
        return fpromise::pending();
      }));
  loop.RunUntilIdle();
  EXPECT_TRUE(suspend);
  EXPECT_FALSE(was_destroyed[0]);

  // Schedule another task that never gets a chance to run.
  executor.schedule_task(
      fpromise::make_promise([d = fit::defer([&] { was_destroyed[1] = true; })] {}));
  EXPECT_FALSE(was_destroyed[1]);

  // Shutdown the loop and ensure that everything was destroyed, including
  // the task that remained suspended.
  loop.Shutdown();
  EXPECT_TRUE(was_destroyed[0]);
  EXPECT_TRUE(was_destroyed[1]);
}

constexpr zx::duration delay = zx::msec(5);

zx::time now() { return zx::clock::get_monotonic(); }

void check_delay(zx::time begin, zx::duration delay) {
  zx::duration actual = now() - begin;
  EXPECT_GE(actual.to_usecs(), delay.to_usecs());
}

TEST(ExecutorTests, delayed_promises) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Executor async_executor(loop.dispatcher());

  struct TaskStats {
    int tasks_planned = 0;
    int tasks_scheduled = 0;
    int tasks_completed = 0;
  } stats;

  class LoggingExecutor final {
   public:
    LoggingExecutor(async::Executor& executor, TaskStats& stats)
        : executor_(executor), stats_(stats) {}

    // This doesn't implement fpromise::executor because we need to chain a then to increment the
    // counter, and we can't do that with fpromise::pending_task
    void schedule_task(fpromise::promise<> task) {
      executor_.schedule_task(task.then([&](fpromise::result<>&) { ++stats_.tasks_completed; }));
      ++stats_.tasks_scheduled;
    }

    async::Executor* operator->() const { return &executor_; }

   private:
    async::Executor& executor_;
    TaskStats& stats_;
  } executor(async_executor, stats);

  auto check = [&](zx::time begin) {
    return [&, begin](fpromise::result<>&) { check_delay(begin, delay); };
  };

  auto check_and_quit = [&](zx::time begin) {
    return [&, begin](fpromise::result<>&) {
      check_delay(begin, delay);
      loop.Quit();
    };
  };

  auto start_loop = [&] {
    return std::thread([&] {
      loop.Run();
      loop.ResetQuit();
    });
  };

  auto check_single = [&](fpromise::promise<> promise, zx::time begin) {
    ++stats.tasks_planned;
    auto loop_thread = start_loop();
    executor.schedule_task(promise.then(check_and_quit(begin)));
    loop_thread.join();
    // Doing this both in and outside the executor in case it doesn't run at all
    check_delay(begin, delay);
  };

  zx::time begin = now();
  zx::time deadline = begin + delay;
  check_single(executor->MakePromiseForTime(deadline), begin);
  check_single(executor->MakeDelayedPromise(delay), begin);

  auto check_combinations = [&](auto&& check) {
    zx::time begin = now();
    zx::time deadline = begin + delay;
    check(executor->MakeDelayedPromise(delay), executor->MakePromiseForTime(deadline), begin);

    begin = now();
    deadline = begin + delay;
    check(executor->MakePromiseForTime(deadline), executor->MakeDelayedPromise(delay), begin);

    begin = now();
    check(executor->MakeDelayedPromise(delay), executor->MakeDelayedPromise(delay), begin);

    begin = now();
    deadline = begin + delay;
    check(executor->MakePromiseForTime(deadline), executor->MakePromiseForTime(deadline), begin);
  };

  // The two promises still take up only |delay| when created at the same time.
  auto check_sequential = [&](fpromise::promise<> first, fpromise::promise<> second,
                              zx::time begin) {
    stats.tasks_planned += 2;
    auto loop_thread = start_loop();
    executor.schedule_task(first.then([&](fpromise::result<>&) {
      check_delay(begin, delay);
      executor.schedule_task(second.then(check_and_quit(begin)));
    }));
    loop_thread.join();
    check_delay(begin, delay);
  };

  auto check_simultaneous = [&](fpromise::promise<> first, fpromise::promise<> second,
                                zx::time begin) {
    stats.tasks_planned += 2;
    auto loop_thread = start_loop();
    executor.schedule_task(first.then(check(begin)));
    executor.schedule_task(second.then(check_and_quit(begin)));
    loop_thread.join();
    check_delay(begin, delay);
  };

  // Even when the returned promise is scheduled late, it still finishes at the right time
  auto check_staggered = [&](fpromise::promise<> first, fpromise::promise<> second,
                             zx::time begin) {
    stats.tasks_planned += 2;
    auto loop_thread = start_loop();
    executor.schedule_task(first.then(check(begin)));
    zx::nanosleep(begin + (delay / 2));
    executor.schedule_task(second.then(check_and_quit(begin)));
    loop_thread.join();
    check_delay(begin, delay);
  };

  check_combinations(check_sequential);
  check_combinations(check_simultaneous);
  check_combinations(check_staggered);

  EXPECT_EQ(stats.tasks_planned, stats.tasks_scheduled);
  EXPECT_EQ(stats.tasks_scheduled, stats.tasks_completed);
}

TEST(ExecutorTests, promise_wait_on_handle) {
  constexpr zx_signals_t trigger = ZX_USER_SIGNAL_0;
  constexpr zx_signals_t other = ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2;
  constexpr zx_signals_t sent = trigger | other;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  constexpr auto check_signaled = [](zx::event& event, zx_signals_t signals) {
    zx_signals_t pending;
    ASSERT_EQ(event.wait_one(0, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
    EXPECT_EQ(pending, signals);
  };

  constexpr auto check_not_signaled = [=](zx::event& event) { check_signaled(event, 0); };

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));
  check_not_signaled(event);

  zx::time begin = now();
  bool completed = false;
  executor.schedule_task(
      executor
          .MakePromiseWaitHandle(zx::unowned_handle(event.get()), trigger, ZX_WAIT_ASYNC_TIMESTAMP)
          .then([&](fpromise::result<zx_packet_signal_t, zx_status_t>& result) {
            ASSERT_FALSE(result.is_pending());
            ASSERT_FALSE(result.is_error());

            check_signaled(event, sent);

            auto packet = result.take_value();
            EXPECT_EQ(packet.trigger, trigger);
            EXPECT_EQ(packet.observed, sent);
            EXPECT_EQ(packet.count, 1);
            EXPECT_GE(zx::time(packet.timestamp) - begin, delay);

            completed = true;
            loop.Quit();
          }));

  std::future<void> run_loop = std::async(std::launch::async, [&] {
    loop.Run();
    loop.ResetQuit();
  });

  std::future<void> signal_promise = std::async(std::launch::async, [&] {
    check_not_signaled(event);
    zx::time deadline = begin + delay;
    zx::nanosleep(deadline);
    // This will queue up on the port but the promise won't be notified about it
    check_not_signaled(event);
    event.signal(0, other);
    check_signaled(event, other);
    event.signal(0, trigger);
    check_signaled(event, sent);
  });

  run_loop.get();
  signal_promise.get();
  check_delay(begin, delay);
  check_signaled(event, sent);

  ASSERT_TRUE(completed);

  // This test demonstrates what happens when you close the handle at various points.
  event.reset();
  ASSERT_OK(zx::event::create(0, &event));
  check_not_signaled(event);

  completed = false;
  executor.schedule_task(executor.MakePromiseWaitHandle(zx::unowned_handle(event.get()), trigger)
                             .then([&](fpromise::result<zx_packet_signal_t, zx_status_t>& result) {
                               EXPECT_TRUE(result.is_ok());

                               auto packet = result.take_value();
                               EXPECT_EQ(packet.trigger, trigger);
                               EXPECT_EQ(packet.observed, trigger);
                               EXPECT_EQ(packet.count, 1);

                               completed = true;
                               loop.Quit();
                             }));

  // Closing the handle before the signal is fired will result in a hang (the signal will never be
  // delivered). However, closing the handle *after* the trigger is sent will still allow the
  // promise to complete, since it already queued on the port.
  //
  // event.reset();
  event.signal(0, trigger);
  event.reset();

  loop.Run();
  loop.ResetQuit();

  ASSERT_TRUE(completed);
}

}  // namespace
