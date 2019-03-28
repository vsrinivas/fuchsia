// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/async_promise/executor.h"

#include <thread>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <unittest/unittest.h>

namespace {

void LaunchAsync(fit::function<void()> func) {
    std::thread thread(std::move(func));
    thread.detach();
}

bool running_tasks() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
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

    END_TEST;
}

bool suspending_and_resuming_tasks() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    async::Executor executor(loop.dispatcher());

    uint64_t run_count[5] = {};
    uint64_t resume_count[5] = {};
    uint64_t resume_count4b = 0;

    // Schedule a task that suspends itself and immediately resumes.
    executor.schedule_task(fit::make_promise([&](fit::context& context)
                                                 -> fit::result<> {
        if (++run_count[0] == 100)
            return fit::ok();
        resume_count[0]++;
        context.suspend_task().resume_task();
        return fit::pending();
    }));

    // Schedule a task that requires several iterations to complete, each
    // time scheduling another task to resume itself after suspension.
    executor.schedule_task(fit::make_promise([&](fit::context& context)
                                                 -> fit::result<> {
        if (++run_count[1] == 100)
            return fit::ok();
        context.executor()->schedule_task(
            fit::make_promise([&, s = context.suspend_task()]() mutable {
                resume_count[1]++;
                s.resume_task();
            }));
        return fit::pending();
    }));

    // Same as the above but use another thread to resume.
    executor.schedule_task(fit::make_promise([&](fit::context& context)
                                                 -> fit::result<> {
        if (++run_count[2] == 100)
            return fit::ok();
        LaunchAsync([&, s = context.suspend_task()]() mutable {
            resume_count[2]++;
            s.resume_task();
        });
        return fit::pending();
    }));

    // Schedule a task that suspends itself but doesn't actually return pending
    // so it only runs once.
    executor.schedule_task(fit::make_promise([&](fit::context& context)
                                                 -> fit::result<> {
        run_count[3]++;
        context.suspend_task();
        return fit::ok();
    }));

    // Schedule a task that suspends itself and arranges to be resumed on
    // one of two other threads, whichever gets there first.
    executor.schedule_task(fit::make_promise([&](fit::context& context)
                                                 -> fit::result<> {
        if (++run_count[4] == 100)
            return fit::ok();
        LaunchAsync([&, s = context.suspend_task()]() mutable {
            resume_count[4]++;
            s.resume_task();
        });
        LaunchAsync([&, s = context.suspend_task()]() mutable {
            resume_count4b++; // use a different variable to avoid data races
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

    END_TEST;
}

bool abandoning_tasks() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    async::Executor executor(loop.dispatcher());
    uint64_t run_count[4] = {};
    uint64_t destruction[4] = {};

    // Schedule a task that returns pending without suspending itself
    // so it is immediately abandoned.
    executor.schedule_task(fit::make_promise(
        [&, d = fit::defer([&] { destruction[0]++; })]() -> fit::result<> {
            run_count[0]++;
            return fit::pending();
        }));

    // Schedule a task that suspends itself but drops the |suspended_task|
    // object before returning so it is immediately abandoned.
    executor.schedule_task(fit::make_promise(
        [&, d = fit::defer([&] { destruction[1]++; })](fit::context& context)
            -> fit::result<> {
            run_count[1]++;
            context.suspend_task(); // ignore result
            return fit::pending();
        }));

    std::thread thread;
    // Schedule a task that suspends itself and drops the |suspended_task|
    // object from a different thread so it is abandoned concurrently.
    executor.schedule_task(fit::make_promise(
        [&, d = fit::defer([&] { destruction[2]++; })](fit::context& context)
            -> fit::result<> {
            run_count[2]++;
            std::thread new_thread([s = context.suspend_task()]() { });
            thread = std::move(new_thread);
            return fit::pending();
        }));

    // Schedule a task that creates several suspended task handles and drops
    // them all on the floor.
    executor.schedule_task(fit::make_promise(
        [&, d = fit::defer([&] { destruction[3]++; })](fit::context& context)
            -> fit::result<> {
            run_count[3]++;
            fit::suspended_task s[3];
            for (size_t i = 0; i < 3; i++)
                s[i] = context.suspend_task();
            return fit::pending();
        }));

    // We expect the tasks to have been executed but to have been abandoned.
    loop.RunUntilIdle();
    thread.join();
    loop.RunUntilIdle();

    EXPECT_EQ(1, run_count[0]);
    EXPECT_EQ(1, destruction[0]);
    EXPECT_EQ(1, run_count[1]);
    EXPECT_EQ(1, destruction[1]);
    EXPECT_EQ(1, run_count[2]);
    EXPECT_EQ(1, destruction[2]);
    EXPECT_EQ(1, run_count[3]);
    EXPECT_EQ(1, destruction[3]);

    END_TEST;
}

bool dispatcher_property() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
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

    END_TEST;
}

bool tasks_scheduled_after_loop_shutdown_are_immediately_destroyed() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    async::Executor executor(loop.dispatcher());

    // Shutdown the loop then schedule a task.
    // The task should be immediately destroyed.
    loop.Shutdown();
    bool was_destroyed = false;
    executor.schedule_task(fit::make_promise(
        [d = fit::defer([&] { was_destroyed = true; })] {}));
    EXPECT_TRUE(was_destroyed);

    END_TEST;
}

bool when_loop_is_shutdown_all_remaining_tasks_are_immediately_destroyed() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
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
    executor.schedule_task(fit::make_promise(
        [d = fit::defer([&] { was_destroyed[1] = true; })] {}));
    EXPECT_FALSE(was_destroyed[1]);

    // Shutdown the loop and ensure that everything was destroyed, including
    // the task that remained suspended.
    loop.Shutdown();
    EXPECT_TRUE(was_destroyed[0]);
    EXPECT_TRUE(was_destroyed[1]);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(executor_tests)
RUN_TEST(running_tasks)
RUN_TEST(suspending_and_resuming_tasks)
RUN_TEST(abandoning_tasks)
RUN_TEST(dispatcher_property)
RUN_TEST(tasks_scheduled_after_loop_shutdown_are_immediately_destroyed)
RUN_TEST(when_loop_is_shutdown_all_remaining_tasks_are_immediately_destroyed)
END_TEST_CASE(executor_tests)
