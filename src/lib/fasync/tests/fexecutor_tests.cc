// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fasync/fexecutor.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <lib/zx/event.h>

#include <future>

#include <zxtest/zxtest.h>

namespace {

TEST(ExecutorTests, running_tasks) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fasync::fexecutor executor(loop.dispatcher());
  uint64_t run_count[3] = {};

  // Schedule a task that runs once and increments a counter.
  executor.schedule(fasync::make_future([&] { run_count[0]++; }));

  // Schedule a task that runs once, increments a counter,
  // and scheduled another task.
  executor.schedule(fasync::make_future([&](fasync::context& context) {
    run_count[1]++;
    assert(&context.executor() == &executor);
    context.executor().schedule(fasync::make_future([&] { run_count[2]++; }));
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
  fasync::fexecutor executor(loop.dispatcher());

  uint64_t run_count[5] = {};
  uint64_t resume_count[5] = {};
  uint64_t resume_count4b = 0;

  // Schedule a task that suspends itself and immediately resumes.
  executor.schedule(fasync::make_future([&](fasync::context& context) -> fasync::poll<> {
    if (++run_count[0] == 100)
      return fasync::done();
    resume_count[0]++;
    context.suspend_task().resume();
    return fasync::pending();
  }));

  // Schedule a task that requires several iterations to complete, each
  // time scheduling another task to resume itself after suspension.
  executor.schedule(fasync::make_future([&](fasync::context& context) -> fasync::poll<> {
    if (++run_count[1] == 100)
      return fasync::done();
    context.executor().schedule(fasync::make_future([&, s = context.suspend_task()]() mutable {
      resume_count[1]++;
      s.resume();
    }));
    return fasync::pending();
  }));

  // Same as the above but use another thread to resume.
  executor.schedule(fasync::make_future([&](fasync::context& context) -> fasync::poll<> {
    if (++run_count[2] == 100)
      return fasync::done();
    [[maybe_unused]] auto a =
        std::async(std::launch::async, [&, s = context.suspend_task()]() mutable {
          resume_count[2]++;
          s.resume();
        });
    return fasync::pending();
  }));

  // Schedule a task that suspends itself but doesn't actually return pending
  // so it only runs once.
  executor.schedule(fasync::make_future([&](fasync::context& context) -> fasync::poll<> {
    run_count[3]++;
    context.suspend_task();
    return fasync::done();
  }));

  // Schedule a task that suspends itself and arranges to be resumed on
  // one of two other threads, whichever gets there first.
  executor.schedule(fasync::make_future([&](fasync::context& context) -> fasync::poll<> {
    if (++run_count[4] == 100)
      return fasync::done();
    [[maybe_unused]] auto a =
        std::async(std::launch::async, [&, s = context.suspend_task()]() mutable {
          resume_count[4]++;
          s.resume();
        });
    [[maybe_unused]] auto b =
        std::async(std::launch::async, [&, s = context.suspend_task()]() mutable {
          resume_count4b++;  // use a different variable to avoid data races
          s.resume();
        });
    return fasync::pending();
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
  fasync::fexecutor executor(loop.dispatcher());
  uint64_t run_count[4] = {};
  uint64_t destruction[4] = {};

  // Schedule a task that returns pending without suspending itself
  // so it is immediately abandoned.
  executor.schedule(
      fasync::make_future([&, d = fit::defer([&] { destruction[0]++; })]() -> fasync::poll<> {
        run_count[0]++;
        return fasync::pending();
      }));

  // Schedule a task that suspends itself but drops the |suspended_task|
  // object before returning so it is immediately abandoned.
  executor.schedule(fasync::make_future(
      [&, d = fit::defer([&] { destruction[1]++; })](fasync::context& context) -> fasync::poll<> {
        run_count[1]++;
        context.suspend_task();  // ignore result
        return fasync::pending();
      }));

  // Schedule a task that suspends itself and drops the |suspended_task|
  // object from a different thread so it is abandoned concurrently.
  executor.schedule(fasync::make_future(
      [&, d = fit::defer([&] { destruction[2]++; })](fasync::context& context) -> fasync::poll<> {
        run_count[2]++;
        [[maybe_unused]] auto a = std::async(std::launch::async, [s = context.suspend_task()] {});
        return fasync::pending();
      }));

  // Schedule a task that creates several suspended task handles and drops
  // them all on the floor.
  executor.schedule(fasync::make_future(
      [&, d = fit::defer([&] { destruction[3]++; })](fasync::context& context) -> fasync::poll<> {
        run_count[3]++;
        fasync::suspended_task s[3];
        for (size_t i = 0; i < 3; i++)
          s[i] = context.suspend_task();
        return fasync::pending();
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
  fasync::fexecutor executor(loop.dispatcher());
  EXPECT_EQ(loop.dispatcher(), executor.dispatcher());

  // Just check that the task receives a context that exposes the dispatcher
  // property.
  async_dispatcher_t* received_dispatcher = nullptr;
  executor.schedule(fasync::make_future([&](fasync::context& context) {
    received_dispatcher = context.as<fasync::fcontext>().dispatcher();
  }));
  EXPECT_NULL(received_dispatcher);

  // We expect that all of the tasks will run to completion.
  loop.RunUntilIdle();
  EXPECT_EQ(loop.dispatcher(), received_dispatcher);
}

TEST(ExecutorTests, tasks_scheduled_after_loop_shutdown_are_immediately_destroyed) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fasync::fexecutor executor(loop.dispatcher());

  // Shutdown the loop then schedule a task.
  // The task should be immediately destroyed.
  loop.Shutdown();
  bool was_destroyed = false;
  executor.schedule(fasync::make_future([d = fit::defer([&] { was_destroyed = true; })] {}));
  EXPECT_TRUE(was_destroyed);
}

TEST(ExecutorTests, when_loop_is_shutdown_all_remaining_tasks_are_immediately_destroyed) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fasync::fexecutor executor(loop.dispatcher());

  // Schedule a task and let it be suspended.
  fasync::suspended_task suspend;
  bool was_destroyed[2] = {};
  executor.schedule(fasync::make_future(
      [&, d = fit::defer([&] { was_destroyed[0] = true; })](fasync::context& context) {
        suspend = context.suspend_task();
        return fasync::pending();
      }));
  loop.RunUntilIdle();
  EXPECT_TRUE(suspend);
  EXPECT_FALSE(was_destroyed[0]);

  // Schedule another task that never gets a chance to run.
  executor.schedule(fasync::make_future([d = fit::defer([&] { was_destroyed[1] = true; })] {}));
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

TEST(ExecutorTests, delayed_futures) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fasync::fexecutor async_executor(loop.dispatcher());

  struct TaskStats {
    int tasks_planned = 0;
    int tasks_scheduled = 0;
    int tasks_completed = 0;
  } stats;

  class LoggingExecutor final {
   public:
    LoggingExecutor(fasync::fexecutor& executor, TaskStats& stats)
        : executor_(executor), stats_(stats) {}

    // This doesn't implement fasync::executor because we need to chain a then to increment the
    // counter, and we can't do that with fasync::pending_task
    void schedule(fasync::future<> task) {
      executor_.schedule(std::move(task) | fasync::then([&] { ++stats_.tasks_completed; }));
      ++stats_.tasks_scheduled;
    }

    fasync::fexecutor* operator->() const { return &executor_; }

   private:
    fasync::fexecutor& executor_;
    TaskStats& stats_;
  } executor(async_executor, stats);

  auto check = [&](zx::time begin) { return [&, begin] { check_delay(begin, delay); }; };

  auto check_and_quit = [&](zx::time begin) {
    return [&, begin] {
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

  auto check_single = [&](fasync::future<> future, zx::time begin) {
    ++stats.tasks_planned;
    auto loop_thread = start_loop();
    executor.schedule(std::move(future) | fasync::then(check_and_quit(begin)));
    loop_thread.join();
    // Doing this both in and outside the executor in case it doesn't run at all
    check_delay(begin, delay);
  };

  zx::time begin = now();
  zx::time deadline = begin + delay;
  check_single(executor->make_future_for_time(deadline), begin);
  check_single(executor->make_delayed_future(delay), begin);

  auto check_combinations = [&](auto&& check) {
    zx::time begin = now();
    zx::time deadline = begin + delay;
    check(executor->make_delayed_future(delay), executor->make_future_for_time(deadline), begin);

    begin = now();
    deadline = begin + delay;
    check(executor->make_future_for_time(deadline), executor->make_delayed_future(delay), begin);

    begin = now();
    check(executor->make_delayed_future(delay), executor->make_delayed_future(delay), begin);

    begin = now();
    deadline = begin + delay;
    check(executor->make_future_for_time(deadline), executor->make_future_for_time(deadline),
          begin);
  };

  // The two futures still take up only |delay| when created at the same time.
  auto check_sequential = [&](fasync::future<> first, fasync::future<> second, zx::time begin) {
    stats.tasks_planned += 2;
    auto loop_thread = start_loop();
    executor.schedule(std::move(first) | fasync::then([&] {
                        check_delay(begin, delay);
                        executor.schedule(std::move(second) | fasync::then(check_and_quit(begin)));
                      }));
    loop_thread.join();
    check_delay(begin, delay);
  };

  auto check_simultaneous = [&](fasync::future<> first, fasync::future<> second, zx::time begin) {
    stats.tasks_planned += 2;
    auto loop_thread = start_loop();
    executor.schedule(std::move(first) | fasync::then(check(begin)));
    executor.schedule(std::move(second) | fasync::then(check_and_quit(begin)));
    loop_thread.join();
    check_delay(begin, delay);
  };

  // Even when the returned future is scheduled late, it still finishes at the right time
  auto check_staggered = [&](fasync::future<> first, fasync::future<> second, zx::time begin) {
    stats.tasks_planned += 2;
    auto loop_thread = start_loop();
    executor.schedule(std::move(first) | fasync::then(check(begin)));
    zx::nanosleep(begin + (delay / 2));
    executor.schedule(std::move(second) | fasync::then(check_and_quit(begin)));
    loop_thread.join();
    check_delay(begin, delay);
  };

  check_combinations(check_sequential);
  check_combinations(check_simultaneous);
  check_combinations(check_staggered);

  EXPECT_EQ(stats.tasks_planned, stats.tasks_scheduled);
  EXPECT_EQ(stats.tasks_scheduled, stats.tasks_completed);
}

TEST(ExecutorTests, future_wait_on_handle) {
  constexpr zx_signals_t trigger = ZX_USER_SIGNAL_0;
  constexpr zx_signals_t other = ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2;
  constexpr zx_signals_t sent = trigger | other;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fasync::fexecutor executor(loop.dispatcher());

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
  executor.schedule(executor.make_future_wait_for_handle(zx::unowned_handle(event.get()), trigger,
                                                         ZX_WAIT_ASYNC_TIMESTAMP) |
                    fasync::then([&](fit::result<zx_status_t, zx_packet_signal_t> result) {
                      ASSERT_FALSE(result.is_error());

                      check_signaled(event, sent);

                      auto packet = std::move(result).value();
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

  std::future<void> signal_future = std::async(std::launch::async, [&] {
    check_not_signaled(event);
    zx::time deadline = begin + delay;
    zx::nanosleep(deadline);
    // This will queue up on the port but the future won't be notified about it
    check_not_signaled(event);
    event.signal(0, other);
    check_signaled(event, other);
    event.signal(0, trigger);
    check_signaled(event, sent);
  });

  run_loop.get();
  signal_future.get();
  check_delay(begin, delay);
  check_signaled(event, sent);

  ASSERT_TRUE(completed);

  // This test demonstrates what happens when you close the handle at various points.
  event.reset();
  ASSERT_OK(zx::event::create(0, &event));
  check_not_signaled(event);

  completed = false;
  executor.schedule(executor.make_future_wait_for_handle(zx::unowned_handle(event.get()), trigger) |
                    fasync::then([&](fit::result<zx_status_t, zx_packet_signal_t> result) {
                      EXPECT_TRUE(result.is_ok());

                      auto packet = std::move(result).value();
                      EXPECT_EQ(packet.trigger, trigger);
                      EXPECT_EQ(packet.observed, trigger);
                      EXPECT_EQ(packet.count, 1);

                      completed = true;
                      loop.Quit();
                    }));

  // Closing the handle before the signal is fired will result in a hang (the signal will never be
  // delivered). However, closing the handle *after* the trigger is sent will still allow the
  // future to complete, since it already queued on the port.
  //
  // event.reset();
  event.signal(0, trigger);
  event.reset();

  loop.Run();
  loop.ResetQuit();

  ASSERT_TRUE(completed);
}

}  // namespace
