// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>

#include <future>

#include <zxtest/zxtest.h>

namespace {

TEST(ExecutorTests, running_tasks) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());
  uint64_t run_count[3] = {};

  // Schedule a task that runs once and increments a counter.
  executor.schedule_task(fit::make_promise([&] { run_count[0]++; }));

  // Schedule a task that runs once, increments a counter,
  // and scheduled another task.
  executor.schedule_task(fit::make_promise([&](fit::context& context) {
    run_count[1]++;
    assert(context.executor() == &executor);
    context.executor()->schedule_task(fit::make_promise([&] { run_count[2]++; }));
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
  executor.schedule_task(fit::make_promise([&](fit::context& context) -> fit::result<> {
    if (++run_count[0] == 100)
      return fit::ok();
    resume_count[0]++;
    context.suspend_task().resume_task();
    return fit::pending();
  }));

  // Schedule a task that requires several iterations to complete, each
  // time scheduling another task to resume itself after suspension.
  executor.schedule_task(fit::make_promise([&](fit::context& context) -> fit::result<> {
    if (++run_count[1] == 100)
      return fit::ok();
    context.executor()->schedule_task(fit::make_promise([&, s = context.suspend_task()]() mutable {
      resume_count[1]++;
      s.resume_task();
    }));
    return fit::pending();
  }));

  // Same as the above but use another thread to resume.
  executor.schedule_task(fit::make_promise([&](fit::context& context) -> fit::result<> {
    if (++run_count[2] == 100)
      return fit::ok();
    std::async(std::launch::async, [&, s = context.suspend_task()]() mutable {
      resume_count[2]++;
      s.resume_task();
    });
    return fit::pending();
  }));

  // Schedule a task that suspends itself but doesn't actually return pending
  // so it only runs once.
  executor.schedule_task(fit::make_promise([&](fit::context& context) -> fit::result<> {
    run_count[3]++;
    context.suspend_task();
    return fit::ok();
  }));

  // Schedule a task that suspends itself and arranges to be resumed on
  // one of two other threads, whichever gets there first.
  executor.schedule_task(fit::make_promise([&](fit::context& context) -> fit::result<> {
    if (++run_count[4] == 100)
      return fit::ok();
    std::async(std::launch::async, [&, s = context.suspend_task()]() mutable {
      resume_count[4]++;
      s.resume_task();
    });
    std::async(std::launch::async, [&, s = context.suspend_task()]() mutable {
      resume_count4b++;  // use a different variable to avoid data races
      s.resume_task();
    });
    return fit::pending();
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
  executor.schedule_task(
      fit::make_promise([&, d = fit::defer([&] { destruction[0]++; })]() -> fit::result<> {
        run_count[0]++;
        return fit::pending();
      }));

  // Schedule a task that suspends itself but drops the |suspended_task|
  // object before returning so it is immediately abandoned.
  executor.schedule_task(fit::make_promise(
      [&, d = fit::defer([&] { destruction[1]++; })](fit::context& context) -> fit::result<> {
        run_count[1]++;
        context.suspend_task();  // ignore result
        return fit::pending();
      }));

  // Schedule a task that suspends itself and drops the |suspended_task|
  // object from a different thread so it is abandoned concurrently.
  executor.schedule_task(fit::make_promise(
      [&, d = fit::defer([&] { destruction[2]++; })](fit::context& context) -> fit::result<> {
        run_count[2]++;
        std::async(std::launch::async, [s = context.suspend_task()] {});
        return fit::pending();
      }));

  // Schedule a task that creates several suspended task handles and drops
  // them all on the floor.
  executor.schedule_task(fit::make_promise(
      [&, d = fit::defer([&] { destruction[3]++; })](fit::context& context) -> fit::result<> {
        run_count[3]++;
        fit::suspended_task s[3];
        for (size_t i = 0; i < 3; i++)
          s[i] = context.suspend_task();
        return fit::pending();
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
  executor.schedule_task(fit::make_promise([&](fit::context& context) {
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
  executor.schedule_task(fit::make_promise([d = fit::defer([&] { was_destroyed = true; })] {}));
  EXPECT_TRUE(was_destroyed);
}

TEST(ExecutorTests, when_loop_is_shutdown_all_remaining_tasks_are_immediately_destroyed) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  // Schedule a task and let it be suspended.
  fit::suspended_task suspend;
  bool was_destroyed[2] = {};
  executor.schedule_task(fit::make_promise(
      [&, d = fit::defer([&] { was_destroyed[0] = true; })](fit::context& context) {
        suspend = context.suspend_task();
        return fit::pending();
      }));
  loop.RunUntilIdle();
  EXPECT_TRUE(suspend);
  EXPECT_FALSE(was_destroyed[0]);

  // Schedule another task that never gets a chance to run.
  executor.schedule_task(fit::make_promise([d = fit::defer([&] { was_destroyed[1] = true; })] {}));
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

    // This doesn't implement fit::executor because we need to chain a then to increment the
    // counter, and we can't do that with fit::pending_task
    void schedule_task(fit::promise<> task) {
      executor_.schedule_task(task.then([&](fit::result<>&) { ++stats_.tasks_completed; }));
      ++stats_.tasks_scheduled;
    }

    async::Executor* operator->() const { return &executor_; }

   private:
    async::Executor& executor_;
    TaskStats& stats_;
  } executor(async_executor, stats);

  auto check = [&](zx::time begin) {
    return [&, begin](fit::result<>&) { check_delay(begin, delay); };
  };

  auto check_and_quit = [&](zx::time begin) {
    return [&, begin](fit::result<>&) {
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

  auto check_single = [&](fit::promise<> promise, zx::time begin) {
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
  auto check_sequential = [&](fit::promise<> first, fit::promise<> second, zx::time begin) {
    stats.tasks_planned += 2;
    auto loop_thread = start_loop();
    executor.schedule_task(first.then([&](fit::result<>&) {
      check_delay(begin, delay);
      executor.schedule_task(second.then(check_and_quit(begin)));
    }));
    loop_thread.join();
    check_delay(begin, delay);
  };

  auto check_simultaneous = [&](fit::promise<> first, fit::promise<> second, zx::time begin) {
    stats.tasks_planned += 2;
    auto loop_thread = start_loop();
    executor.schedule_task(first.then(check(begin)));
    executor.schedule_task(second.then(check_and_quit(begin)));
    loop_thread.join();
    check_delay(begin, delay);
  };

  // Even when the returned promise is scheduled late, it still finishes at the right time
  auto check_staggered = [&](fit::promise<> first, fit::promise<> second, zx::time begin) {
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

}  // namespace
