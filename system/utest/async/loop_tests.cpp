// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <magenta/syscalls.h>

#include <async/loop.h>
#include <async/receiver.h>
#include <async/task.h>
#include <async/wait.h>

#include <mx/event.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/mutex.h>
#include <unittest/unittest.h>

namespace {

inline mx_time_t now() {
    return mx_time_get(MX_CLOCK_MONOTONIC);
}

class TestWait {
public:
    TestWait(mx_handle_t object, mx_signals_t trigger)
        : op(object, trigger) {
        op.set_handler(fbl::BindMember(this, &TestWait::Handle));
    }

    virtual ~TestWait() = default;

    async::Wait op;
    uint32_t run_count = 0u;
    mx_status_t last_status = MX_ERR_INTERNAL;
    const mx_packet_signal_t* last_signal = nullptr;

protected:
    virtual async_wait_result_t Handle(async_t* async, mx_status_t status,
                                       const mx_packet_signal_t* signal) {
        run_count++;
        last_status = status;
        if (signal) {
            last_signal_storage_ = *signal;
            last_signal = &last_signal_storage_;
        } else {
            last_signal = nullptr;
        }
        return ASYNC_WAIT_FINISHED;
    }

private:
    mx_packet_signal_t last_signal_storage_;
};

class CascadeWait : public TestWait {
public:
    CascadeWait(mx_handle_t object, mx_signals_t trigger,
                mx_signals_t signals_to_clear, mx_signals_t signals_to_set,
                bool repeat)
        : TestWait(object, trigger),
          signals_to_clear_(signals_to_clear),
          signals_to_set_(signals_to_set),
          repeat_(repeat) {}

protected:
    mx_signals_t signals_to_clear_;
    mx_signals_t signals_to_set_;
    bool repeat_;

    async_wait_result_t Handle(async_t* async, mx_status_t status,
                               const mx_packet_signal_t* signal) override {
        TestWait::Handle(async, status, signal);
        mx::unowned_event::wrap(op.object()).signal(signals_to_clear_, signals_to_set_);
        return repeat_ && status == MX_OK ? ASYNC_WAIT_AGAIN : ASYNC_WAIT_FINISHED;
    }
};

class TestTask {
public:
    TestTask(mx_time_t deadline)
        : op(deadline) {
        op.set_handler(fbl::BindMember(this, &TestTask::Handle));
    }

    virtual ~TestTask() = default;

    async::Task op;
    uint32_t run_count = 0u;
    mx_status_t last_status = MX_ERR_INTERNAL;

protected:
    virtual async_task_result_t Handle(async_t* async, mx_status_t status) {
        run_count++;
        last_status = status;
        return ASYNC_TASK_FINISHED;
    }
};

class QuitTask : public TestTask {
public:
    QuitTask(mx_time_t deadline = now())
        : TestTask(deadline) {}

protected:
    async_task_result_t Handle(async_t* async, mx_status_t status) override {
        TestTask::Handle(async, status);
        async_loop_quit(async);
        return ASYNC_TASK_FINISHED;
    }
};

class ResetQuitTask : public TestTask {
public:
    ResetQuitTask(mx_time_t deadline = now())
        : TestTask(deadline) {}

    mx_status_t result = MX_ERR_INTERNAL;

protected:
    async_task_result_t Handle(async_t* async, mx_status_t status) override {
        TestTask::Handle(async, status);
        result = async_loop_reset_quit(async);
        return ASYNC_TASK_FINISHED;
    }
};

class RepeatingTask : public TestTask {
public:
    RepeatingTask(mx_time_t deadline, mx_duration_t interval, uint32_t repeat_count)
        : TestTask(deadline), interval_(interval), repeat_count_(repeat_count) {}

    void set_finish_callback(fbl::Closure callback) {
        finish_callback_ = fbl::move(callback);
    }

protected:
    mx_duration_t interval_;
    uint32_t repeat_count_;
    fbl::Closure finish_callback_;

    async_task_result_t Handle(async_t* async, mx_status_t status) override {
        TestTask::Handle(async, status);
        op.set_deadline(op.deadline() + interval_);
        if (repeat_count_ == 0) {
            if (finish_callback_)
                finish_callback_();
            return ASYNC_TASK_FINISHED;
        }
        repeat_count_ -= 1;
        return status == MX_OK ? ASYNC_TASK_REPEAT : ASYNC_TASK_FINISHED;
    }
};

class TestReceiver {
public:
    TestReceiver() {
        op.set_handler(fbl::BindMember(this, &TestReceiver::Handle));
    }

    virtual ~TestReceiver() = default;

    async::Receiver op;
    uint32_t run_count = 0u;
    mx_status_t last_status = MX_ERR_INTERNAL;
    const mx_packet_user_t* last_data;

protected:
    virtual void Handle(async_t* async, mx_status_t status, const mx_packet_user_t* data) {
        run_count++;
        last_status = status;
        if (data) {
            last_data_storage_ = *data;
            last_data = &last_data_storage_;
        } else {
            last_data = nullptr;
        }
    }

private:
    mx_packet_user_t last_data_storage_{};
};

// The C++ loop wrapper is one-to-one with the underlying C API so for the
// most part we will test through that interface but here we make sure that
// the C API actually exists but we don't comprehensively test what it does.
bool c_api_basic_test() {
    BEGIN_TEST;

    async_t* async;
    ASSERT_EQ(MX_OK, async_loop_create(nullptr, &async), "create");
    ASSERT_NONNULL(async, "async");

    EXPECT_EQ(ASYNC_LOOP_RUNNABLE, async_loop_get_state(async), "runnable");

    async_loop_quit(async);
    EXPECT_EQ(ASYNC_LOOP_QUIT, async_loop_get_state(async), "quitting");
    async_loop_run(async, MX_TIME_INFINITE, false);
    EXPECT_EQ(MX_OK, async_loop_reset_quit(async));

    thrd_t thread{};
    EXPECT_EQ(MX_OK, async_loop_start_thread(async, "name", &thread), "thread start");
    EXPECT_NE(thrd_t{}, thread, "thread ws initialized");
    async_loop_quit(async);
    async_loop_join_threads(async);

    async_loop_shutdown(async);
    EXPECT_EQ(ASYNC_LOOP_SHUTDOWN, async_loop_get_state(async), "shutdown");

    async_loop_destroy(async);

    END_TEST;
}

bool make_default_false_test() {
    BEGIN_TEST;

    {
        async::Loop loop;
        EXPECT_NULL(async_get_default(), "not default");
    }
    EXPECT_NULL(async_get_default(), "still not default");

    END_TEST;
}

bool make_default_true_test() {
    BEGIN_TEST;

    async_loop_config_t config{};
    config.make_default_for_current_thread = true;
    {
        async::Loop loop(&config);
        EXPECT_EQ(loop.async(), async_get_default(), "became default");
    }
    EXPECT_NULL(async_get_default(), "no longer default");

    END_TEST;
}

bool quit_test() {
    BEGIN_TEST;

    async::Loop loop;
    EXPECT_EQ(ASYNC_LOOP_RUNNABLE, loop.GetState(), "initially not quitting");

    loop.Quit();
    EXPECT_EQ(ASYNC_LOOP_QUIT, loop.GetState(), "quitting when quit");
    EXPECT_EQ(MX_ERR_CANCELED, loop.Run(), "run returns immediately");
    EXPECT_EQ(ASYNC_LOOP_QUIT, loop.GetState(), "still quitting");

    ResetQuitTask reset_quit_task;
    EXPECT_EQ(MX_OK, reset_quit_task.op.Post(loop.async()), "can post tasks even after quit");
    QuitTask quit_task;
    EXPECT_EQ(MX_OK, quit_task.op.Post(loop.async()), "can post tasks even after quit");

    EXPECT_EQ(MX_OK, loop.ResetQuit());
    EXPECT_EQ(ASYNC_LOOP_RUNNABLE, loop.GetState(), "not quitting after reset");

    EXPECT_EQ(MX_OK, loop.Run(MX_TIME_INFINITE, true /*once*/), "run tasks");

    EXPECT_EQ(1u, reset_quit_task.run_count, "reset quit task ran");
    EXPECT_EQ(MX_ERR_BAD_STATE, reset_quit_task.result, "can't reset quit while loop is running");

    EXPECT_EQ(1u, quit_task.run_count, "quit task ran");
    EXPECT_EQ(ASYNC_LOOP_QUIT, loop.GetState(), "quitted");

    EXPECT_EQ(MX_ERR_CANCELED, loop.Run(), "runs returns immediately when quitted");

    loop.Shutdown();
    EXPECT_EQ(ASYNC_LOOP_SHUTDOWN, loop.GetState(), "shut down");
    EXPECT_EQ(MX_ERR_BAD_STATE, loop.Run(), "run returns immediately when shut down");
    EXPECT_EQ(MX_ERR_BAD_STATE, loop.ResetQuit());

    END_TEST;
}

bool wait_test() {
    BEGIN_TEST;

    async::Loop loop;
    mx::event event;
    EXPECT_EQ(MX_OK, mx::event::create(0u, &event), "create event");

    CascadeWait wait1(event.get(), MX_USER_SIGNAL_1,
                      0u, MX_USER_SIGNAL_2, false);
    CascadeWait wait2(event.get(), MX_USER_SIGNAL_2,
                      MX_USER_SIGNAL_1 | MX_USER_SIGNAL_2, 0u, true);
    CascadeWait wait3(event.get(), MX_USER_SIGNAL_3,
                      MX_USER_SIGNAL_3, 0u, true);
    EXPECT_EQ(MX_OK, wait1.op.Begin(loop.async()), "wait 1");
    EXPECT_EQ(MX_OK, wait2.op.Begin(loop.async()), "wait 2");
    EXPECT_EQ(MX_OK, wait3.op.Begin(loop.async()), "wait 3");

    // Initially nothing is signaled.
    EXPECT_EQ(MX_ERR_TIMED_OUT, loop.Run(mx_deadline_after(MX_MSEC(1))), "run loop");
    EXPECT_EQ(0u, wait1.run_count, "run count 1");
    EXPECT_EQ(0u, wait2.run_count, "run count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");

    // Set signal 1: notifies |wait1| which sets signal 2 and notifies |wait2|
    // which clears signal 1 and 2 again.
    EXPECT_EQ(MX_OK, event.signal(0u, MX_USER_SIGNAL_1), "signal 1");
    EXPECT_EQ(MX_ERR_TIMED_OUT, loop.Run(mx_deadline_after(MX_MSEC(1))), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(MX_OK, wait1.last_status, "status 1");
    EXPECT_NONNULL(wait1.last_signal);
    EXPECT_EQ(MX_USER_SIGNAL_1, wait1.last_signal->trigger & MX_USER_SIGNAL_ALL, "trigger 1");
    EXPECT_EQ(MX_USER_SIGNAL_1, wait1.last_signal->observed & MX_USER_SIGNAL_ALL, "observed 1");
    EXPECT_EQ(1u, wait1.last_signal->count, "count 1");
    EXPECT_EQ(1u, wait2.run_count, "run count 2");
    EXPECT_EQ(MX_OK, wait2.last_status, "status 2");
    EXPECT_NONNULL(wait2.last_signal);
    EXPECT_EQ(MX_USER_SIGNAL_2, wait2.last_signal->trigger & MX_USER_SIGNAL_ALL, "trigger 2");
    EXPECT_EQ(MX_USER_SIGNAL_1 | MX_USER_SIGNAL_2, wait2.last_signal->observed & MX_USER_SIGNAL_ALL, "observed 2");
    EXPECT_EQ(1u, wait2.last_signal->count, "count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");

    // Set signal 1 again: does nothing because |wait1| was a one-shot.
    EXPECT_EQ(MX_OK, event.signal(0u, MX_USER_SIGNAL_1), "signal 1");
    EXPECT_EQ(MX_ERR_TIMED_OUT, loop.Run(mx_deadline_after(MX_MSEC(1))), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(1u, wait2.run_count, "run count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");

    // Set signal 2 again: notifies |wait2| which clears signal 1 and 2 again.
    EXPECT_EQ(MX_OK, event.signal(0u, MX_USER_SIGNAL_2), "signal 2");
    EXPECT_EQ(MX_ERR_TIMED_OUT, loop.Run(mx_deadline_after(MX_MSEC(1))), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(2u, wait2.run_count, "run count 2");
    EXPECT_EQ(MX_OK, wait2.last_status, "status 2");
    EXPECT_NONNULL(wait2.last_signal);
    EXPECT_EQ(MX_USER_SIGNAL_2, wait2.last_signal->trigger & MX_USER_SIGNAL_ALL, "trigger 2");
    EXPECT_EQ(MX_USER_SIGNAL_1 | MX_USER_SIGNAL_2, wait2.last_signal->observed & MX_USER_SIGNAL_ALL, "observed 2");
    EXPECT_EQ(1u, wait2.last_signal->count, "count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");

    // Set signal 3: notifies |wait3| which clears signal 3.
    // Do this a couple of times.
    for (uint32_t i = 0; i < 3; i++) {
        EXPECT_EQ(MX_OK, event.signal(0u, MX_USER_SIGNAL_3), "signal 3");
        EXPECT_EQ(MX_ERR_TIMED_OUT, loop.Run(mx_deadline_after(MX_MSEC(1))), "run loop");
        EXPECT_EQ(1u, wait1.run_count, "run count 1");
        EXPECT_EQ(2u, wait2.run_count, "run count 2");
        EXPECT_EQ(i + 1u, wait3.run_count, "run count 3");
        EXPECT_EQ(MX_OK, wait3.last_status, "status 3");
        EXPECT_NONNULL(wait3.last_signal);
        EXPECT_EQ(MX_USER_SIGNAL_3, wait3.last_signal->trigger & MX_USER_SIGNAL_ALL, "trigger 3");
        EXPECT_EQ(MX_USER_SIGNAL_3, wait3.last_signal->observed & MX_USER_SIGNAL_ALL, "observed 3");
        EXPECT_EQ(1u, wait3.last_signal->count, "count 3");
    }

    // Cancel wait 3 then set signal 3 again: nothing happens this time.
    EXPECT_EQ(MX_OK, wait3.op.Cancel(loop.async()), "cancel");
    EXPECT_EQ(MX_OK, event.signal(0u, MX_USER_SIGNAL_3), "signal 3");
    EXPECT_EQ(MX_ERR_TIMED_OUT, loop.Run(mx_deadline_after(MX_MSEC(1))), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(2u, wait2.run_count, "run count 2");
    EXPECT_EQ(3u, wait3.run_count, "run count 3");

    // Redundant cancel returns an error.
    EXPECT_EQ(MX_ERR_NOT_FOUND, wait3.op.Cancel(loop.async()), "cancel again");
    EXPECT_EQ(MX_ERR_TIMED_OUT, loop.Run(mx_deadline_after(MX_MSEC(1))), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(2u, wait2.run_count, "run count 2");
    EXPECT_EQ(3u, wait3.run_count, "run count 3");

    END_TEST;
}

bool wait_invalid_handle_test() {
    BEGIN_TEST;

    async::Loop loop;

    TestWait wait(MX_HANDLE_INVALID, MX_USER_SIGNAL_0);
    EXPECT_EQ(MX_ERR_BAD_HANDLE, wait.op.Begin(loop.async()), "begin");
    EXPECT_EQ(MX_ERR_BAD_HANDLE, wait.op.Cancel(loop.async()), "cancel");
    EXPECT_EQ(MX_ERR_TIMED_OUT, loop.Run(mx_deadline_after(MX_MSEC(1))), "run loop");
    EXPECT_EQ(0u, wait.run_count, "run count");

    END_TEST;
}

bool wait_shutdown_test() {
    BEGIN_TEST;

    async::Loop loop;
    mx::event event;
    EXPECT_EQ(MX_OK, mx::event::create(0u, &event), "create event");

    CascadeWait wait1(event.get(), MX_USER_SIGNAL_0, 0u, 0u, false);
    wait1.op.set_flags(ASYNC_FLAG_HANDLE_SHUTDOWN);
    CascadeWait wait2(event.get(), MX_USER_SIGNAL_0, MX_USER_SIGNAL_0, 0u, true);
    wait2.op.set_flags(ASYNC_FLAG_HANDLE_SHUTDOWN);
    TestWait wait3(event.get(), MX_USER_SIGNAL_1);
    wait3.op.set_flags(ASYNC_FLAG_HANDLE_SHUTDOWN);
    TestWait wait4(event.get(), MX_USER_SIGNAL_1);

    EXPECT_EQ(MX_OK, wait1.op.Begin(loop.async()), "begin 1");
    EXPECT_EQ(MX_OK, wait2.op.Begin(loop.async()), "begin 2");
    EXPECT_EQ(MX_OK, wait3.op.Begin(loop.async()), "begin 3");
    EXPECT_EQ(MX_OK, wait4.op.Begin(loop.async()), "begin 4");

    // Nothing signaled so nothing happens at first.
    EXPECT_EQ(MX_ERR_TIMED_OUT, loop.Run(mx_deadline_after(MX_MSEC(1))), "run loop");
    EXPECT_EQ(0u, wait1.run_count, "run count 1");
    EXPECT_EQ(0u, wait2.run_count, "run count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");
    EXPECT_EQ(0u, wait4.run_count, "run count 4");

    // Set signal 1: notifies both waiters, |wait2| clears the signal and repeats
    EXPECT_EQ(MX_OK, event.signal(0u, MX_USER_SIGNAL_0), "signal 1");
    EXPECT_EQ(MX_ERR_TIMED_OUT, loop.Run(mx_deadline_after(MX_MSEC(1))), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(MX_OK, wait1.last_status, "status 1");
    EXPECT_NONNULL(wait1.last_signal);
    EXPECT_EQ(MX_USER_SIGNAL_0, wait1.last_signal->trigger & MX_USER_SIGNAL_ALL, "trigger 1");
    EXPECT_EQ(MX_USER_SIGNAL_0, wait1.last_signal->observed & MX_USER_SIGNAL_ALL, "observed 1");
    EXPECT_EQ(1u, wait1.last_signal->count, "count 1");
    EXPECT_EQ(1u, wait2.run_count, "run count 2");
    EXPECT_EQ(MX_OK, wait2.last_status, "status 2");
    EXPECT_NONNULL(wait2.last_signal);
    EXPECT_EQ(MX_USER_SIGNAL_0, wait2.last_signal->trigger & MX_USER_SIGNAL_ALL, "trigger 2");
    EXPECT_EQ(MX_USER_SIGNAL_0, wait2.last_signal->observed & MX_USER_SIGNAL_ALL, "observed 2");
    EXPECT_EQ(1u, wait2.last_signal->count, "count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");
    EXPECT_EQ(0u, wait4.run_count, "run count 4");

    // When the loop shuts down:
    //   |wait1| not notified because it was serviced and didn't repeat
    //   |wait2| notified because it repeated
    //   |wait3| notified because it was not yet serviced
    //   |wait4| not notified because it didn't ask to handle shutdown
    loop.Shutdown();
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(2u, wait2.run_count, "run count 2");
    EXPECT_EQ(MX_ERR_CANCELED, wait2.last_status, "status 2");
    EXPECT_NULL(wait2.last_signal, "signal 2");
    EXPECT_EQ(1u, wait3.run_count, "run count 3");
    EXPECT_EQ(MX_ERR_CANCELED, wait3.last_status, "status 3");
    EXPECT_NULL(wait3.last_signal, "signal 3");
    EXPECT_EQ(0u, wait4.run_count, "run count 4");

    // Try to add or cancel work after shutdown.
    TestWait wait5(event.get(), MX_USER_SIGNAL_0);
    EXPECT_EQ(MX_ERR_BAD_STATE, wait5.op.Begin(loop.async()), "begin after shutdown");
    EXPECT_EQ(MX_ERR_NOT_FOUND, wait5.op.Cancel(loop.async()), "cancel after shutdown");
    EXPECT_EQ(0u, wait5.run_count, "run count 5");

    END_TEST;
}

bool task_test() {
    BEGIN_TEST;

    async::Loop loop;

    mx_time_t start_time = now();
    TestTask task1(start_time + MX_MSEC(1));
    RepeatingTask task2(start_time + MX_MSEC(1), MX_MSEC(1), 3u);
    TestTask task3(start_time);
    QuitTask task4(start_time + MX_MSEC(10));
    TestTask task5(start_time + MX_MSEC(10)); // posted after quit

    EXPECT_EQ(MX_OK, task1.op.Post(loop.async()), "post 1");
    EXPECT_EQ(MX_OK, task2.op.Post(loop.async()), "post 2");
    EXPECT_EQ(MX_OK, task3.op.Post(loop.async()), "post 3");
    task2.set_finish_callback([&loop, &task4, &task5] {
        task4.op.Post(loop.async());
        task5.op.Post(loop.async());
    });

    // Cancel task 3.
    EXPECT_EQ(MX_OK, task3.op.Cancel(loop.async()), "cancel 3");

    // Run until quit.
    EXPECT_EQ(MX_ERR_CANCELED, loop.Run(), "run loop");
    EXPECT_EQ(ASYNC_LOOP_QUIT, loop.GetState(), "quitting");
    EXPECT_EQ(1u, task1.run_count, "run count 1");
    EXPECT_EQ(MX_OK, task1.last_status, "status 1");
    EXPECT_EQ(4u, task2.run_count, "run count 2");
    EXPECT_EQ(MX_OK, task2.last_status, "status 2");
    EXPECT_EQ(0u, task3.run_count, "run count 3");
    EXPECT_EQ(1u, task4.run_count, "run count 4");
    EXPECT_EQ(MX_OK, task4.last_status, "status 4");
    EXPECT_EQ(0u, task5.run_count, "run count 5");

    // Reset quit and keep running, now task5 should go ahead followed
    // by any subsequently posted tasks even if they have earlier deadlines.
    QuitTask task6(start_time);
    TestTask task7(start_time);
    EXPECT_EQ(MX_OK, task6.op.Post(loop.async()), "post 6");
    EXPECT_EQ(MX_OK, task7.op.Post(loop.async()), "post 7");
    EXPECT_EQ(MX_OK, loop.ResetQuit());
    EXPECT_EQ(MX_ERR_CANCELED, loop.Run(), "run loop");
    EXPECT_EQ(ASYNC_LOOP_QUIT, loop.GetState(), "quitting");

    EXPECT_EQ(1u, task5.run_count, "run count 5");
    EXPECT_EQ(MX_OK, task5.last_status, "status 5");
    EXPECT_EQ(1u, task6.run_count, "run count 6");
    EXPECT_EQ(MX_OK, task6.last_status, "status 6");
    EXPECT_EQ(0u, task7.run_count, "run count 7");

    END_TEST;
}

bool task_shutdown_test() {
    BEGIN_TEST;

    async::Loop loop;

    mx_time_t start_time = now();
    TestTask task1(start_time + MX_MSEC(1));
    task1.op.set_flags(ASYNC_FLAG_HANDLE_SHUTDOWN);
    RepeatingTask task2(start_time + MX_MSEC(1), MX_MSEC(1000), 1u);
    task2.op.set_flags(ASYNC_FLAG_HANDLE_SHUTDOWN);
    TestTask task3(MX_TIME_INFINITE);
    task3.op.set_flags(ASYNC_FLAG_HANDLE_SHUTDOWN);
    TestTask task4(MX_TIME_INFINITE);
    task4.op.set_flags(ASYNC_FLAG_HANDLE_SHUTDOWN);
    TestTask task5(MX_TIME_INFINITE);
    QuitTask task6(start_time + MX_MSEC(1));

    EXPECT_EQ(MX_OK, task1.op.Post(loop.async()), "post 1");
    EXPECT_EQ(MX_OK, task2.op.Post(loop.async()), "post 2");
    EXPECT_EQ(MX_OK, task3.op.Post(loop.async()), "post 3");
    EXPECT_EQ(MX_OK, task4.op.Post(loop.async()), "post 4");
    EXPECT_EQ(MX_OK, task5.op.Post(loop.async()), "post 5");
    EXPECT_EQ(MX_OK, task6.op.Post(loop.async()), "post 6");

    // Run tasks which are due up to the time when the quit task runs.
    EXPECT_EQ(MX_ERR_CANCELED, loop.Run(), "run loop");
    EXPECT_EQ(1u, task1.run_count, "run count 1");
    EXPECT_EQ(MX_OK, task1.last_status, "status 1");
    EXPECT_EQ(1u, task2.run_count, "run count 2");
    EXPECT_EQ(MX_OK, task2.last_status, "status 2");
    EXPECT_EQ(0u, task3.run_count, "run count 3");
    EXPECT_EQ(0u, task4.run_count, "run count 4");
    EXPECT_EQ(0u, task5.run_count, "run count 5");
    EXPECT_EQ(1u, task6.run_count, "run count 6");
    EXPECT_EQ(MX_OK, task6.last_status, "status 6");

    // Cancel task 4.
    EXPECT_EQ(MX_OK, task4.op.Cancel(loop.async()), "cancel 4");

    // When the loop shuts down:
    //   |task1| not notified because it was serviced
    //   |task2| notified because it requested a repeat
    //   |task3| notified because it was not yet serviced
    //   |task4| not notified because it was canceled
    //   |task5| not notified because it didn't ask to handle shutdown
    //   |task6| not notified because it was serviced
    loop.Shutdown();
    EXPECT_EQ(1u, task1.run_count, "run count 1");
    EXPECT_EQ(2u, task2.run_count, "run count 2");
    EXPECT_EQ(MX_ERR_CANCELED, task2.last_status, "status 2");
    EXPECT_EQ(1u, task3.run_count, "run count 3");
    EXPECT_EQ(MX_ERR_CANCELED, task3.last_status, "status 3");
    EXPECT_EQ(0u, task4.run_count, "run count 4");
    EXPECT_EQ(0u, task5.run_count, "run count 5");
    EXPECT_EQ(1u, task6.run_count, "run count 6");

    // Try to add or cancel work after shutdown.
    TestTask task7(MX_TIME_INFINITE);
    EXPECT_EQ(MX_ERR_BAD_STATE, task7.op.Post(loop.async()), "post after shutdown");
    EXPECT_EQ(MX_ERR_NOT_FOUND, task7.op.Cancel(loop.async()), "cancel after shutdown");
    EXPECT_EQ(0u, task7.run_count, "run count 7");

    END_TEST;
}

bool receiver_test() {
    const mx_packet_user_t data1{.u64 = {11, 12, 13, 14}};
    const mx_packet_user_t data2{.u64 = {21, 22, 23, 24}};
    const mx_packet_user_t data3{.u64 = {31, 32, 33, 34}};
    const mx_packet_user_t data_default{};

    BEGIN_TEST;

    async::Loop loop;

    TestReceiver receiver1;
    TestReceiver receiver2;
    TestReceiver receiver3;

    EXPECT_EQ(MX_OK, receiver1.op.Queue(loop.async(), &data1), "queue 1");
    EXPECT_EQ(MX_OK, receiver1.op.Queue(loop.async(), &data3), "queue 1, again");
    EXPECT_EQ(MX_OK, receiver2.op.Queue(loop.async(), &data2), "queue 2");
    EXPECT_EQ(MX_OK, receiver3.op.Queue(loop.async()), "queue 3");

    EXPECT_EQ(MX_ERR_TIMED_OUT, loop.Run(mx_deadline_after(MX_MSEC(1))), "run loop");
    EXPECT_EQ(2u, receiver1.run_count, "run count 1");
    EXPECT_EQ(MX_OK, receiver1.last_status, "status 1");
    EXPECT_NONNULL(receiver1.last_data);
    EXPECT_EQ(0, memcmp(&data3, receiver1.last_data, sizeof(mx_packet_user_t)), "data 1");
    EXPECT_EQ(1u, receiver2.run_count, "run count 2");
    EXPECT_EQ(MX_OK, receiver2.last_status, "status 2");
    EXPECT_NONNULL(receiver2.last_data);
    EXPECT_EQ(0, memcmp(&data2, receiver2.last_data, sizeof(mx_packet_user_t)), "data 2");
    EXPECT_EQ(1u, receiver3.run_count, "run count 3");
    EXPECT_EQ(MX_OK, receiver3.last_status, "status 3");
    EXPECT_NONNULL(receiver3.last_data);
    EXPECT_EQ(0, memcmp(&data_default, receiver3.last_data, sizeof(mx_packet_user_t)), "data 3");

    END_TEST;
}

bool receiver_shutdown_test() {
    BEGIN_TEST;

    async::Loop loop;
    loop.Shutdown();

    // Try to add work after shutdown.
    TestReceiver receiver;
    EXPECT_EQ(MX_ERR_BAD_STATE, receiver.op.Queue(loop.async()), "queue after shutdown");
    EXPECT_EQ(0u, receiver.run_count, "run count 1");

    END_TEST;
}

class GetDefaultDispatcherTask : public QuitTask {
public:
    async_t* last_default_dispatcher;

protected:
    async_task_result_t Handle(async_t* async, mx_status_t status) override {
        QuitTask::Handle(async, status);
        last_default_dispatcher = async_get_default();
        return ASYNC_TASK_FINISHED;
    }
};

class ConcurrencyMeasure {
public:
    ConcurrencyMeasure(uint32_t end)
        : end_(end) {}

    uint32_t max_threads() const { return fbl::atomic_load(&max_threads_, fbl::memory_order_acquire); }
    uint32_t count() const { return fbl::atomic_load(&count_, fbl::memory_order_acquire); }

    void Tally(async_t* async) {
        // Increment count of concurrently active threads.  Update maximum if needed.
        uint32_t active = 1u + fbl::atomic_fetch_add(&active_threads_, 1u,
                                                      fbl::memory_order_acq_rel);
        uint32_t old_max;
        do {
            old_max = fbl::atomic_load(&max_threads_, fbl::memory_order_acquire);
        } while (active > old_max &&
                 !fbl::atomic_compare_exchange_weak(&max_threads_, &old_max, active,
                                                     fbl::memory_order_acq_rel,
                                                     fbl::memory_order_acquire));

        // Pretend to do work.
        mx_nanosleep(mx_deadline_after(MX_MSEC(1)));

        // Decrement count of active threads.
        fbl::atomic_fetch_sub(&active_threads_, 1u, fbl::memory_order_acq_rel);

        // Quit when last item processed.
        if (1u + fbl::atomic_fetch_add(&count_, 1u, fbl::memory_order_acq_rel) == end_)
            async_loop_quit(async);
    }

private:
    const uint32_t end_;
    fbl::atomic_uint32_t count_{};
    fbl::atomic_uint32_t active_threads_{};
    fbl::atomic_uint32_t max_threads_{};
};

class ThreadAssertWait : public TestWait {
public:
    ThreadAssertWait(mx_handle_t object, mx_signals_t trigger, ConcurrencyMeasure* measure)
        : TestWait(object, trigger), measure_(measure) {}

protected:
    ConcurrencyMeasure* measure_;

    async_wait_result_t Handle(async_t* async, mx_status_t status,
                               const mx_packet_signal_t* signal) override {
        TestWait::Handle(async, status, signal);
        measure_->Tally(async);
        return ASYNC_WAIT_FINISHED;
    }
};

class ThreadAssertTask : public TestTask {
public:
    ThreadAssertTask(mx_time_t deadline, ConcurrencyMeasure* measure)
        : TestTask(deadline), measure_(measure) {}

protected:
    ConcurrencyMeasure* measure_;

    async_task_result_t Handle(async_t* async, mx_status_t status) override {
        TestTask::Handle(async, status);
        measure_->Tally(async);
        return ASYNC_TASK_FINISHED;
    }
};

class ThreadAssertReceiver : public TestReceiver {
public:
    ThreadAssertReceiver(ConcurrencyMeasure* measure)
        : measure_(measure) {}

protected:
    ConcurrencyMeasure* measure_;

    // This receiver's handler will run concurrently on multiple threads
    // (unlike the Waits and Tasks) so we must guard its state.
    fbl::Mutex mutex_;

    void Handle(async_t* async, mx_status_t status, const mx_packet_user_t* data) override {
        {
            fbl::AutoLock lock(&mutex_);
            TestReceiver::Handle(async, status, data);
        }
        measure_->Tally(async);
    }
};

bool threads_have_default_dispatcher() {
    BEGIN_TEST;

    async::Loop loop;
    EXPECT_EQ(MX_OK, loop.StartThread(), "start thread");

    GetDefaultDispatcherTask task;
    EXPECT_EQ(MX_OK, task.op.Post(loop.async()), "post task");
    loop.JoinThreads();

    EXPECT_EQ(1u, task.run_count, "run count");
    EXPECT_EQ(MX_OK, task.last_status, "status");
    EXPECT_EQ(loop.async(), task.last_default_dispatcher, "default dispatcher");

    END_TEST;
}

// The goal here is to ensure that threads stop when Quit() is called.
bool threads_quit() {
    const size_t num_threads = 4;

    BEGIN_TEST;

    async::Loop loop;
    for (size_t i = 0; i < num_threads; i++) {
        EXPECT_EQ(MX_OK, loop.StartThread());
    }
    loop.Quit();
    loop.JoinThreads();
    EXPECT_EQ(ASYNC_LOOP_QUIT, loop.GetState());

    END_TEST;
}

// The goal here is to ensure that threads stop when Shutdown() is called.
bool threads_shutdown() {
    const size_t num_threads = 4;

    BEGIN_TEST;

    async::Loop loop;
    for (size_t i = 0; i < num_threads; i++) {
        EXPECT_EQ(MX_OK, loop.StartThread());
    }
    loop.Shutdown();
    EXPECT_EQ(ASYNC_LOOP_SHUTDOWN, loop.GetState());

    loop.JoinThreads(); // should be a no-op

    EXPECT_EQ(MX_ERR_BAD_STATE, loop.StartThread(), "can't start threads after shutdown");

    END_TEST;
}

// The goal here is to schedule a lot of work and see whether it runs
// on as many threads as we expected it to.
bool threads_waits_run_concurrently_test() {
    const size_t num_threads = 4;
    const size_t num_items = 100;

    BEGIN_TEST;

    async::Loop loop;
    for (size_t i = 0; i < num_threads; i++) {
        EXPECT_EQ(MX_OK, loop.StartThread(), "start thread");
    }

    ConcurrencyMeasure measure(num_items);
    mx::event event;
    EXPECT_EQ(MX_OK, mx::event::create(0u, &event), "create event");
    EXPECT_EQ(MX_OK, event.signal(0u, MX_USER_SIGNAL_0), "signal");

    // Post a number of work items to run all at once.
    ThreadAssertWait* items[num_items];
    for (size_t i = 0; i < num_items; i++) {
        items[i] = new ThreadAssertWait(event.get(), MX_USER_SIGNAL_0, &measure);
        EXPECT_EQ(MX_OK, items[i]->op.Begin(loop.async()), "begin wait");
    }

    // Wait until quitted.
    loop.JoinThreads();

    // Ensure all work items completed.
    EXPECT_EQ(num_items, measure.count(), "item count");
    for (size_t i = 0; i < num_items; i++) {
        EXPECT_EQ(1u, items[i]->run_count, "run count");
        EXPECT_EQ(MX_OK, items[i]->last_status, "status");
        EXPECT_NONNULL(items[i]->last_signal, "signal");
        EXPECT_EQ(MX_USER_SIGNAL_0, items[i]->last_signal->observed & MX_USER_SIGNAL_ALL, "observed");
        delete items[i];
    }

    // Ensure that we actually ran many waits concurrently on different threads.
    EXPECT_NE(1u, measure.max_threads(), "waits handled concurrently");

    END_TEST;
}

// The goal here is to schedule a lot of work and see whether it runs
// on as many threads as we expected it to.
bool threads_tasks_run_sequentially_test() {
    const size_t num_threads = 4;
    const size_t num_items = 100;

    BEGIN_TEST;

    async::Loop loop;
    for (size_t i = 0; i < num_threads; i++) {
        EXPECT_EQ(MX_OK, loop.StartThread(), "start thread");
    }

    ConcurrencyMeasure measure(num_items);

    // Post a number of work items to run all at once.
    ThreadAssertTask* items[num_items];
    mx_time_t start_time = now();
    for (size_t i = 0; i < num_items; i++) {
        items[i] = new ThreadAssertTask(start_time + MX_MSEC(i), &measure);
        EXPECT_EQ(MX_OK, items[i]->op.Post(loop.async()), "post task");
    }

    // Wait until quitted.
    loop.JoinThreads();

    // Ensure all work items completed.
    EXPECT_EQ(num_items, measure.count(), "item count");
    for (size_t i = 0; i < num_items; i++) {
        EXPECT_EQ(1u, items[i]->run_count, "run count");
        EXPECT_EQ(MX_OK, items[i]->last_status, "status");
        delete items[i];
    }

    // Ensure that we actually ran tasks sequentially despite having many
    // threads available.
    EXPECT_EQ(1u, measure.max_threads(), "tasks handled sequentially");

    END_TEST;
}

// The goal here is to schedule a lot of work and see whether it runs
// on as many threads as we expected it to.
bool threads_receivers_run_concurrently_test() {
    const size_t num_threads = 4;
    const size_t num_items = 100;

    BEGIN_TEST;

    async::Loop loop;
    for (size_t i = 0; i < num_threads; i++) {
        EXPECT_EQ(MX_OK, loop.StartThread(), "start thread");
    }

    ConcurrencyMeasure measure(num_items);

    // Post a number of packets all at once.
    ThreadAssertReceiver receiver(&measure);
    for (size_t i = 0; i < num_items; i++) {
        EXPECT_EQ(MX_OK, receiver.op.Queue(loop.async()), "queue packet");
    }

    // Wait until quitted.
    loop.JoinThreads();

    // Ensure all work items completed.
    EXPECT_EQ(num_items, measure.count(), "item count");
    EXPECT_EQ(num_items, receiver.run_count, "run count");
    EXPECT_EQ(MX_OK, receiver.last_status, "status");

    // Ensure that we actually processed many packets concurrently on different threads.
    EXPECT_NE(1u, measure.max_threads(), "packets handled concurrently");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(loop_tests)
RUN_TEST(c_api_basic_test)
RUN_TEST(make_default_false_test)
RUN_TEST(make_default_true_test)
RUN_TEST(quit_test)
RUN_TEST(wait_test)
RUN_TEST(wait_invalid_handle_test)
RUN_TEST(wait_shutdown_test)
RUN_TEST(task_test)
RUN_TEST(task_shutdown_test)
RUN_TEST(receiver_test)
RUN_TEST(receiver_shutdown_test)
RUN_TEST(threads_have_default_dispatcher)
for (int i = 0; i < 3; i++) {
    RUN_TEST(threads_quit)
    RUN_TEST(threads_shutdown)
    RUN_TEST(threads_waits_run_concurrently_test)
    RUN_TEST(threads_tasks_run_sequentially_test)
    RUN_TEST(threads_receivers_run_concurrently_test)
}
END_TEST_CASE(loop_tests)
