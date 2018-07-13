// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <zircon/syscalls.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/time.h>
#include <lib/async/receiver.h>
#include <lib/async/task.h>
#include <lib/async/time.h>
#include <lib/async/wait.h>

#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/mutex.h>
#include <lib/zx/event.h>
#include <unittest/unittest.h>

namespace {

class TestWait : public async_wait_t {
public:
    TestWait(zx_handle_t object, zx_signals_t trigger)
        : async_wait_t{{ASYNC_STATE_INIT}, &TestWait::CallHandler, object, trigger} {
    }

    virtual ~TestWait() = default;

    uint32_t run_count = 0u;
    zx_status_t last_status = ZX_ERR_INTERNAL;
    const zx_packet_signal_t* last_signal = nullptr;

    zx_status_t Begin(async_dispatcher_t* dispatcher) {
        return async_begin_wait(dispatcher, this);
    }

    zx_status_t Cancel(async_dispatcher_t* dispatcher) {
        return async_cancel_wait(dispatcher, this);
    }

protected:
    virtual void Handle(async_dispatcher_t* dispatcher, zx_status_t status,
                        const zx_packet_signal_t* signal) {
        run_count++;
        last_status = status;
        if (signal) {
            last_signal_storage_ = *signal;
            last_signal = &last_signal_storage_;
        } else {
            last_signal = nullptr;
        }
    }

private:
    static void CallHandler(async_dispatcher_t* dispatcher, async_wait_t* wait,
                            zx_status_t status, const zx_packet_signal_t* signal) {
        static_cast<TestWait*>(wait)->Handle(dispatcher, status, signal);
    }

    zx_packet_signal_t last_signal_storage_;
};

class CascadeWait : public TestWait {
public:
    CascadeWait(zx_handle_t object, zx_signals_t trigger,
                zx_signals_t signals_to_clear, zx_signals_t signals_to_set,
                bool repeat)
        : TestWait(object, trigger),
          signals_to_clear_(signals_to_clear),
          signals_to_set_(signals_to_set),
          repeat_(repeat) {}

protected:
    zx_signals_t signals_to_clear_;
    zx_signals_t signals_to_set_;
    bool repeat_;

    void Handle(async_dispatcher_t* dispatcher, zx_status_t status,
                const zx_packet_signal_t* signal) override {
        TestWait::Handle(dispatcher, status, signal);
        zx_object_signal(object, signals_to_clear_, signals_to_set_);
        if (repeat_ && status == ZX_OK) {
            Begin(dispatcher);
        }
    }
};

class SelfCancelingWait : public TestWait {
public:
    SelfCancelingWait(zx_handle_t object, zx_signals_t trigger)
        : TestWait(object, trigger) {}

    zx_status_t cancel_result = ZX_ERR_INTERNAL;

protected:
    void Handle(async_dispatcher_t* dispatcher, zx_status_t status,
                const zx_packet_signal_t* signal) override {
        TestWait::Handle(dispatcher, status, signal);
        cancel_result = Cancel(dispatcher);
    }
};

class TestTask : public async_task_t {
public:
    TestTask()
        : async_task_t{{ASYNC_STATE_INIT}, &TestTask::CallHandler, ZX_TIME_INFINITE} {}

    virtual ~TestTask() = default;

    zx_status_t Post(async_dispatcher_t* dispatcher) {
        this->deadline = async_now(dispatcher);
        return async_post_task(dispatcher, this);
    }

    zx_status_t PostForTime(async_dispatcher_t* dispatcher, zx::time deadline) {
        this->deadline = deadline.get();
        return async_post_task(dispatcher, this);
    }

    zx_status_t Cancel(async_dispatcher_t* dispatcher) {
        return async_cancel_task(dispatcher, this);
    }

    uint32_t run_count = 0u;
    zx_status_t last_status = ZX_ERR_INTERNAL;

protected:
    virtual void Handle(async_dispatcher_t* dispatcher, zx_status_t status) {
        run_count++;
        last_status = status;
    }

private:
    static void CallHandler(async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status) {
        static_cast<TestTask*>(task)->Handle(dispatcher, status);
    }
};

class QuitTask : public TestTask {
public:
    QuitTask() = default;

protected:
    void Handle(async_dispatcher_t* dispatcher, zx_status_t status) override {
        TestTask::Handle(dispatcher, status);
        async_loop_quit(async_loop_from_dispatcher(dispatcher));
    }
};

class ResetQuitTask : public TestTask {
public:
    ResetQuitTask() = default;

    zx_status_t result = ZX_ERR_INTERNAL;

protected:
    void Handle(async_dispatcher_t* dispatcher, zx_status_t status) override {
        TestTask::Handle(dispatcher, status);
        result = async_loop_reset_quit(async_loop_from_dispatcher(dispatcher));
    }
};

class RepeatingTask : public TestTask {
public:
    RepeatingTask(zx::duration interval, uint32_t repeat_count)
        : interval_(interval), repeat_count_(repeat_count) {}

    void set_finish_callback(fbl::Closure callback) {
        finish_callback_ = fbl::move(callback);
    }

protected:
    zx::duration interval_;
    uint32_t repeat_count_;
    fbl::Closure finish_callback_;

    void Handle(async_dispatcher_t* dispatcher, zx_status_t status) override {
        TestTask::Handle(dispatcher, status);
        if (repeat_count_ == 0) {
            if (finish_callback_)
                finish_callback_();
        } else {
            repeat_count_ -= 1;
            if (status == ZX_OK) {
                deadline += interval_.get();
                Post(dispatcher);
            }
        }
    }
};

class SelfCancelingTask : public TestTask {
public:
    SelfCancelingTask() = default;

    zx_status_t cancel_result = ZX_ERR_INTERNAL;

protected:
    void Handle(async_dispatcher_t* dispatcher, zx_status_t status) override {
        TestTask::Handle(dispatcher, status);
        cancel_result = Cancel(dispatcher);
    }
};

class TestReceiver : async_receiver_t {
public:
    TestReceiver()
        : async_receiver_t{{ASYNC_STATE_INIT}, &TestReceiver::CallHandler} {
    }

    virtual ~TestReceiver() = default;

    zx_status_t QueuePacket(async_dispatcher_t* dispatcher, const zx_packet_user_t* data) {
        return async_queue_packet(dispatcher, this, data);
    }

    uint32_t run_count = 0u;
    zx_status_t last_status = ZX_ERR_INTERNAL;
    const zx_packet_user_t* last_data;

protected:
    virtual void Handle(async_dispatcher_t* dispatcher, zx_status_t status,
                        const zx_packet_user_t* data) {
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
    static void CallHandler(async_dispatcher_t* dispatcher, async_receiver_t* receiver,
                            zx_status_t status, const zx_packet_user_t* data) {
        static_cast<TestReceiver*>(receiver)->Handle(dispatcher, status, data);
    }

    zx_packet_user_t last_data_storage_{};
};

// The C++ loop wrapper is one-to-one with the underlying C API so for the
// most part we will test through that interface but here we make sure that
// the C API actually exists but we don't comprehensively test what it does.
bool c_api_basic_test() {
    BEGIN_TEST;

    async_loop_t* loop;
    ASSERT_EQ(ZX_OK, async_loop_create(&kAsyncLoopConfigNoAttachToThread, &loop), "create");
    ASSERT_NONNULL(loop, "loop");

    EXPECT_EQ(ASYNC_LOOP_RUNNABLE, async_loop_get_state(loop), "runnable");

    async_loop_quit(loop);
    EXPECT_EQ(ASYNC_LOOP_QUIT, async_loop_get_state(loop), "quitting");
    async_loop_run(loop, ZX_TIME_INFINITE, false);
    EXPECT_EQ(ZX_OK, async_loop_reset_quit(loop));

    thrd_t thread{};
    EXPECT_EQ(ZX_OK, async_loop_start_thread(loop, "name", &thread), "thread start");
    EXPECT_NE(thrd_t{}, thread, "thread ws initialized");
    async_loop_quit(loop);
    async_loop_join_threads(loop);

    async_loop_shutdown(loop);
    EXPECT_EQ(ASYNC_LOOP_SHUTDOWN, async_loop_get_state(loop), "shutdown");

    async_loop_destroy(loop);

    END_TEST;
}

bool make_default_false_test() {
    BEGIN_TEST;

    {
        async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
        EXPECT_NULL(async_get_default_dispatcher(), "not default");
    }
    EXPECT_NULL(async_get_default_dispatcher(), "still not default");

    END_TEST;
}

bool make_default_true_test() {
    BEGIN_TEST;

    async_loop_config_t config{};
    config.make_default_for_current_thread = true;
    {
        async::Loop loop(&config);
        EXPECT_EQ(loop.dispatcher(), async_get_default_dispatcher(), "became default");
    }
    EXPECT_NULL(async_get_default_dispatcher(), "no longer default");

    END_TEST;
}

bool create_default_test() {
    BEGIN_TEST;

    {
        async::Loop loop(&kAsyncLoopConfigAttachToThread);
        EXPECT_EQ(loop.dispatcher(), async_get_default_dispatcher(), "became default");
    }
    EXPECT_NULL(async_get_default_dispatcher(), "no longer default");

    END_TEST;
}

bool quit_test() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    EXPECT_EQ(ASYNC_LOOP_RUNNABLE, loop.GetState(), "initially not quitting");

    loop.Quit();
    EXPECT_EQ(ASYNC_LOOP_QUIT, loop.GetState(), "quitting when quit");
    EXPECT_EQ(ZX_ERR_CANCELED, loop.Run(), "run returns immediately");
    EXPECT_EQ(ASYNC_LOOP_QUIT, loop.GetState(), "still quitting");

    ResetQuitTask reset_quit_task;
    EXPECT_EQ(ZX_OK, reset_quit_task.Post(loop.dispatcher()), "can post tasks even after quit");
    QuitTask quit_task;
    EXPECT_EQ(ZX_OK, quit_task.Post(loop.dispatcher()), "can post tasks even after quit");

    EXPECT_EQ(ZX_OK, loop.ResetQuit());
    EXPECT_EQ(ASYNC_LOOP_RUNNABLE, loop.GetState(), "not quitting after reset");

    EXPECT_EQ(ZX_OK, loop.Run(zx::time::infinite(), true /*once*/), "run tasks");

    EXPECT_EQ(1u, reset_quit_task.run_count, "reset quit task ran");
    EXPECT_EQ(ZX_ERR_BAD_STATE, reset_quit_task.result, "can't reset quit while loop is running");

    EXPECT_EQ(1u, quit_task.run_count, "quit task ran");
    EXPECT_EQ(ASYNC_LOOP_QUIT, loop.GetState(), "quitted");

    EXPECT_EQ(ZX_ERR_CANCELED, loop.Run(), "runs returns immediately when quitted");

    loop.Shutdown();
    EXPECT_EQ(ASYNC_LOOP_SHUTDOWN, loop.GetState(), "shut down");
    EXPECT_EQ(ZX_ERR_BAD_STATE, loop.Run(), "run returns immediately when shut down");
    EXPECT_EQ(ZX_ERR_BAD_STATE, loop.ResetQuit());

    END_TEST;
}

bool time_test() {
    BEGIN_TEST;

    // Verify that the dispatcher's time-telling is strictly monotonic,
    // which is constent with ZX_CLOCK_MONOTONIC.
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    zx::time t0 = zx::clock::get_monotonic();
    zx::time t1 = async::Now(loop.dispatcher());
    zx::time t2 = async::Now(loop.dispatcher());
    zx::time t3 = zx::clock::get_monotonic();

    EXPECT_LE(t0.get(), t1.get());
    EXPECT_LE(t1.get(), t2.get());
    EXPECT_LE(t2.get(), t3.get());

    END_TEST;
}

bool wait_test() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    zx::event event;
    EXPECT_EQ(ZX_OK, zx::event::create(0u, &event), "create event");

    CascadeWait wait1(event.get(), ZX_USER_SIGNAL_1,
                      0u, ZX_USER_SIGNAL_2, false);
    CascadeWait wait2(event.get(), ZX_USER_SIGNAL_2,
                      ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2, 0u, true);
    CascadeWait wait3(event.get(), ZX_USER_SIGNAL_3,
                      ZX_USER_SIGNAL_3, 0u, true);
    EXPECT_EQ(ZX_OK, wait1.Begin(loop.dispatcher()), "wait 1");
    EXPECT_EQ(ZX_OK, wait2.Begin(loop.dispatcher()), "wait 2");
    EXPECT_EQ(ZX_OK, wait3.Begin(loop.dispatcher()), "wait 3");

    // Initially nothing is signaled.
    EXPECT_EQ(ZX_OK, loop.RunUntilIdle(), "run loop");
    EXPECT_EQ(0u, wait1.run_count, "run count 1");
    EXPECT_EQ(0u, wait2.run_count, "run count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");

    // Set signal 1: notifies |wait1| which sets signal 2 and notifies |wait2|
    // which clears signal 1 and 2 again.
    EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_1), "signal 1");
    EXPECT_EQ(ZX_OK, loop.RunUntilIdle(), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(ZX_OK, wait1.last_status, "status 1");
    EXPECT_NONNULL(wait1.last_signal);
    EXPECT_EQ(ZX_USER_SIGNAL_1, wait1.last_signal->trigger & ZX_USER_SIGNAL_ALL, "trigger 1");
    EXPECT_EQ(ZX_USER_SIGNAL_1, wait1.last_signal->observed & ZX_USER_SIGNAL_ALL, "observed 1");
    EXPECT_EQ(1u, wait1.last_signal->count, "count 1");
    EXPECT_EQ(1u, wait2.run_count, "run count 2");
    EXPECT_EQ(ZX_OK, wait2.last_status, "status 2");
    EXPECT_NONNULL(wait2.last_signal);
    EXPECT_EQ(ZX_USER_SIGNAL_2, wait2.last_signal->trigger & ZX_USER_SIGNAL_ALL, "trigger 2");
    EXPECT_EQ(ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2, wait2.last_signal->observed & ZX_USER_SIGNAL_ALL, "observed 2");
    EXPECT_EQ(1u, wait2.last_signal->count, "count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");

    // Set signal 1 again: does nothing because |wait1| was a one-shot.
    EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_1), "signal 1");
    EXPECT_EQ(ZX_OK, loop.RunUntilIdle(), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(1u, wait2.run_count, "run count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");

    // Set signal 2 again: notifies |wait2| which clears signal 1 and 2 again.
    EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_2), "signal 2");
    EXPECT_EQ(ZX_OK, loop.RunUntilIdle(), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(2u, wait2.run_count, "run count 2");
    EXPECT_EQ(ZX_OK, wait2.last_status, "status 2");
    EXPECT_NONNULL(wait2.last_signal);
    EXPECT_EQ(ZX_USER_SIGNAL_2, wait2.last_signal->trigger & ZX_USER_SIGNAL_ALL, "trigger 2");
    EXPECT_EQ(ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2, wait2.last_signal->observed & ZX_USER_SIGNAL_ALL, "observed 2");
    EXPECT_EQ(1u, wait2.last_signal->count, "count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");

    // Set signal 3: notifies |wait3| which clears signal 3.
    // Do this a couple of times.
    for (uint32_t i = 0; i < 3; i++) {
        EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_3), "signal 3");
        EXPECT_EQ(ZX_OK, loop.RunUntilIdle(), "run loop");
        EXPECT_EQ(1u, wait1.run_count, "run count 1");
        EXPECT_EQ(2u, wait2.run_count, "run count 2");
        EXPECT_EQ(i + 1u, wait3.run_count, "run count 3");
        EXPECT_EQ(ZX_OK, wait3.last_status, "status 3");
        EXPECT_NONNULL(wait3.last_signal);
        EXPECT_EQ(ZX_USER_SIGNAL_3, wait3.last_signal->trigger & ZX_USER_SIGNAL_ALL, "trigger 3");
        EXPECT_EQ(ZX_USER_SIGNAL_3, wait3.last_signal->observed & ZX_USER_SIGNAL_ALL, "observed 3");
        EXPECT_EQ(1u, wait3.last_signal->count, "count 3");
    }

    // Cancel wait 3 then set signal 3 again: nothing happens this time.
    EXPECT_EQ(ZX_OK, wait3.Cancel(loop.dispatcher()), "cancel");
    EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_3), "signal 3");
    EXPECT_EQ(ZX_OK, loop.RunUntilIdle(), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(2u, wait2.run_count, "run count 2");
    EXPECT_EQ(3u, wait3.run_count, "run count 3");

    // Redundant cancel returns an error.
    EXPECT_EQ(ZX_ERR_NOT_FOUND, wait3.Cancel(loop.dispatcher()), "cancel again");
    EXPECT_EQ(ZX_OK, loop.RunUntilIdle(), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(2u, wait2.run_count, "run count 2");
    EXPECT_EQ(3u, wait3.run_count, "run count 3");

    loop.Shutdown();

    END_TEST;
}

bool wait_unwaitable_handle_test() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    zx::event event;
    EXPECT_EQ(ZX_OK, zx::event::create(0u, &event), "create event");
    event.replace(ZX_RIGHT_NONE, &event);

    TestWait wait(event.get(), ZX_USER_SIGNAL_0);
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, wait.Begin(loop.dispatcher()), "begin");
    EXPECT_EQ(ZX_ERR_NOT_FOUND, wait.Cancel(loop.dispatcher()), "cancel");
    EXPECT_EQ(ZX_OK, loop.RunUntilIdle(), "run loop");
    EXPECT_EQ(0u, wait.run_count, "run count");

    END_TEST;
}

bool wait_shutdown_test() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    zx::event event;
    EXPECT_EQ(ZX_OK, zx::event::create(0u, &event), "create event");

    CascadeWait wait1(event.get(), ZX_USER_SIGNAL_0, 0u, 0u, false);
    CascadeWait wait2(event.get(), ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_0, 0u, true);
    TestWait wait3(event.get(), ZX_USER_SIGNAL_1);
    SelfCancelingWait wait4(event.get(), ZX_USER_SIGNAL_0);
    SelfCancelingWait wait5(event.get(), ZX_USER_SIGNAL_1);

    EXPECT_EQ(ZX_OK, wait1.Begin(loop.dispatcher()), "begin 1");
    EXPECT_EQ(ZX_OK, wait2.Begin(loop.dispatcher()), "begin 2");
    EXPECT_EQ(ZX_OK, wait3.Begin(loop.dispatcher()), "begin 3");
    EXPECT_EQ(ZX_OK, wait4.Begin(loop.dispatcher()), "begin 4");
    EXPECT_EQ(ZX_OK, wait5.Begin(loop.dispatcher()), "begin 5");

    // Nothing signaled so nothing happens at first.
    EXPECT_EQ(ZX_OK, loop.RunUntilIdle(), "run loop");
    EXPECT_EQ(0u, wait1.run_count, "run count 1");
    EXPECT_EQ(0u, wait2.run_count, "run count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");
    EXPECT_EQ(0u, wait4.run_count, "run count 4");
    EXPECT_EQ(0u, wait5.run_count, "run count 5");

    // Set signal 1: notifies both waiters, |wait2| clears the signal and repeats
    EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0), "signal 1");
    EXPECT_EQ(ZX_OK, loop.RunUntilIdle(), "run loop");
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(ZX_OK, wait1.last_status, "status 1");
    EXPECT_NONNULL(wait1.last_signal);
    EXPECT_EQ(ZX_USER_SIGNAL_0, wait1.last_signal->trigger & ZX_USER_SIGNAL_ALL, "trigger 1");
    EXPECT_EQ(ZX_USER_SIGNAL_0, wait1.last_signal->observed & ZX_USER_SIGNAL_ALL, "observed 1");
    EXPECT_EQ(1u, wait1.last_signal->count, "count 1");
    EXPECT_EQ(1u, wait2.run_count, "run count 2");
    EXPECT_EQ(ZX_OK, wait2.last_status, "status 2");
    EXPECT_NONNULL(wait2.last_signal);
    EXPECT_EQ(ZX_USER_SIGNAL_0, wait2.last_signal->trigger & ZX_USER_SIGNAL_ALL, "trigger 2");
    EXPECT_EQ(ZX_USER_SIGNAL_0, wait2.last_signal->observed & ZX_USER_SIGNAL_ALL, "observed 2");
    EXPECT_EQ(1u, wait2.last_signal->count, "count 2");
    EXPECT_EQ(0u, wait3.run_count, "run count 3");
    EXPECT_EQ(1u, wait4.run_count, "run count 4");
    EXPECT_EQ(ZX_USER_SIGNAL_0, wait4.last_signal->trigger & ZX_USER_SIGNAL_ALL, "trigger 4");
    EXPECT_EQ(ZX_USER_SIGNAL_0, wait4.last_signal->observed & ZX_USER_SIGNAL_ALL, "observed 4");
    EXPECT_EQ(ZX_ERR_NOT_FOUND, wait4.cancel_result, "cancel result 4");
    EXPECT_EQ(0u, wait5.run_count, "run count 5");

    // When the loop shuts down:
    //   |wait1| not notified because it was serviced and didn't repeat
    //   |wait2| notified because it repeated
    //   |wait3| notified because it was not yet serviced
    //   |wait4| not notified because it was serviced
    //   |wait5| notified because it was not yet serviced
    loop.Shutdown();
    EXPECT_EQ(1u, wait1.run_count, "run count 1");
    EXPECT_EQ(2u, wait2.run_count, "run count 2");
    EXPECT_EQ(ZX_ERR_CANCELED, wait2.last_status, "status 2");
    EXPECT_NULL(wait2.last_signal, "signal 2");
    EXPECT_EQ(1u, wait3.run_count, "run count 3");
    EXPECT_EQ(ZX_ERR_CANCELED, wait3.last_status, "status 3");
    EXPECT_NULL(wait3.last_signal, "signal 3");
    EXPECT_EQ(1u, wait4.run_count, "run count 4");
    EXPECT_EQ(1u, wait5.run_count, "run count 5");
    EXPECT_EQ(ZX_ERR_CANCELED, wait5.last_status, "status 5");
    EXPECT_NULL(wait5.last_signal, "signal 5");
    EXPECT_EQ(ZX_ERR_NOT_FOUND, wait5.cancel_result, "cancel result 5");

    // Try to add or cancel work after shutdown.
    TestWait wait6(event.get(), ZX_USER_SIGNAL_0);
    EXPECT_EQ(ZX_ERR_BAD_STATE, wait6.Begin(loop.dispatcher()), "begin after shutdown");
    EXPECT_EQ(ZX_ERR_NOT_FOUND, wait6.Cancel(loop.dispatcher()), "cancel after shutdown");
    EXPECT_EQ(0u, wait6.run_count, "run count 6");

    END_TEST;
}

bool task_test() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

    zx::time start_time = async::Now(loop.dispatcher());
    TestTask task1;
    RepeatingTask task2(zx::msec(1), 3u);
    TestTask task3;
    QuitTask task4;
    TestTask task5; // posted after quit

    EXPECT_EQ(ZX_OK, task1.PostForTime(loop.dispatcher(), start_time + zx::msec(1)), "post 1");
    EXPECT_EQ(ZX_OK, task2.PostForTime(loop.dispatcher(), start_time + zx::msec(1)), "post 2");
    EXPECT_EQ(ZX_OK, task3.PostForTime(loop.dispatcher(), start_time), "post 3");
    task2.set_finish_callback([&loop, &task4, &task5, start_time] {
        task4.PostForTime(loop.dispatcher(), start_time + zx::msec(10));
        task5.PostForTime(loop.dispatcher(), start_time + zx::msec(10));
    });

    // Cancel task 3.
    EXPECT_EQ(ZX_OK, task3.Cancel(loop.dispatcher()), "cancel 3");

    // Run until quit.
    EXPECT_EQ(ZX_ERR_CANCELED, loop.Run(), "run loop");
    EXPECT_EQ(ASYNC_LOOP_QUIT, loop.GetState(), "quitting");
    EXPECT_EQ(1u, task1.run_count, "run count 1");
    EXPECT_EQ(ZX_OK, task1.last_status, "status 1");
    EXPECT_EQ(4u, task2.run_count, "run count 2");
    EXPECT_EQ(ZX_OK, task2.last_status, "status 2");
    EXPECT_EQ(0u, task3.run_count, "run count 3");
    EXPECT_EQ(1u, task4.run_count, "run count 4");
    EXPECT_EQ(ZX_OK, task4.last_status, "status 4");
    EXPECT_EQ(0u, task5.run_count, "run count 5");

    // Reset quit and keep running, now task5 should go ahead followed
    // by any subsequently posted tasks even if they have earlier deadlines.
    QuitTask task6;
    TestTask task7;
    EXPECT_EQ(ZX_OK, task6.PostForTime(loop.dispatcher(), start_time), "post 6");
    EXPECT_EQ(ZX_OK, task7.PostForTime(loop.dispatcher(), start_time), "post 7");
    EXPECT_EQ(ZX_OK, loop.ResetQuit());
    EXPECT_EQ(ZX_ERR_CANCELED, loop.Run(), "run loop");
    EXPECT_EQ(ASYNC_LOOP_QUIT, loop.GetState(), "quitting");

    EXPECT_EQ(1u, task5.run_count, "run count 5");
    EXPECT_EQ(ZX_OK, task5.last_status, "status 5");
    EXPECT_EQ(1u, task6.run_count, "run count 6");
    EXPECT_EQ(ZX_OK, task6.last_status, "status 6");
    EXPECT_EQ(0u, task7.run_count, "run count 7");

    loop.Shutdown();

    END_TEST;
}

bool task_shutdown_test() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

    zx::time start_time = async::Now(loop.dispatcher());
    TestTask task1;
    RepeatingTask task2(zx::msec(1000), 1u);
    TestTask task3;
    TestTask task4;
    QuitTask task5;
    SelfCancelingTask task6;
    SelfCancelingTask task7;

    EXPECT_EQ(ZX_OK, task1.PostForTime(loop.dispatcher(), start_time + zx::msec(1)), "post 1");
    EXPECT_EQ(ZX_OK, task2.PostForTime(loop.dispatcher(), start_time + zx::msec(1)), "post 2");
    EXPECT_EQ(ZX_OK, task3.PostForTime(loop.dispatcher(), zx::time::infinite()), "post 3");
    EXPECT_EQ(ZX_OK, task4.PostForTime(loop.dispatcher(), zx::time::infinite()), "post 4");
    EXPECT_EQ(ZX_OK, task5.PostForTime(loop.dispatcher(), start_time + zx::msec(1)), "post 5");
    EXPECT_EQ(ZX_OK, task6.PostForTime(loop.dispatcher(), start_time), "post 6");
    EXPECT_EQ(ZX_OK, task7.PostForTime(loop.dispatcher(), zx::time::infinite()), "post 7");

    // Run tasks which are due up to the time when the quit task runs.
    EXPECT_EQ(ZX_ERR_CANCELED, loop.Run(), "run loop");
    EXPECT_EQ(1u, task1.run_count, "run count 1");
    EXPECT_EQ(ZX_OK, task1.last_status, "status 1");
    EXPECT_EQ(1u, task2.run_count, "run count 2");
    EXPECT_EQ(ZX_OK, task2.last_status, "status 2");
    EXPECT_EQ(0u, task3.run_count, "run count 3");
    EXPECT_EQ(0u, task4.run_count, "run count 4");
    EXPECT_EQ(1u, task5.run_count, "run count 5");
    EXPECT_EQ(ZX_OK, task5.last_status, "status 5");
    EXPECT_EQ(1u, task6.run_count, "run count 6");
    EXPECT_EQ(ZX_OK, task6.last_status, "status 6");
    EXPECT_EQ(ZX_ERR_NOT_FOUND, task6.cancel_result, "cancel result 6");
    EXPECT_EQ(0u, task7.run_count, "run count 7");

    // Cancel task 4.
    EXPECT_EQ(ZX_OK, task4.Cancel(loop.dispatcher()), "cancel 4");

    // When the loop shuts down:
    //   |task1| not notified because it was serviced
    //   |task2| notified because it requested a repeat
    //   |task3| notified because it was not yet serviced
    //   |task4| not notified because it was canceled
    //   |task5| not notified because it was serviced
    //   |task6| not notified because it was serviced
    //   |task7| notified because it was not yet serviced
    loop.Shutdown();
    EXPECT_EQ(1u, task1.run_count, "run count 1");
    EXPECT_EQ(2u, task2.run_count, "run count 2");
    EXPECT_EQ(ZX_ERR_CANCELED, task2.last_status, "status 2");
    EXPECT_EQ(1u, task3.run_count, "run count 3");
    EXPECT_EQ(ZX_ERR_CANCELED, task3.last_status, "status 3");
    EXPECT_EQ(0u, task4.run_count, "run count 4");
    EXPECT_EQ(1u, task5.run_count, "run count 5");
    EXPECT_EQ(1u, task6.run_count, "run count 6");
    EXPECT_EQ(1u, task7.run_count, "run count 7");
    EXPECT_EQ(ZX_ERR_CANCELED, task7.last_status, "status 7");
    EXPECT_EQ(ZX_ERR_NOT_FOUND, task7.cancel_result, "cancel result 7");

    // Try to add or cancel work after shutdown.
    TestTask task8;
    EXPECT_EQ(ZX_ERR_BAD_STATE, task8.PostForTime(loop.dispatcher(), zx::time::infinite()), "post after shutdown");
    EXPECT_EQ(ZX_ERR_NOT_FOUND, task8.Cancel(loop.dispatcher()), "cancel after shutdown");
    EXPECT_EQ(0u, task8.run_count, "run count 8");

    END_TEST;
}

bool receiver_test() {
    const zx_packet_user_t data1{.u64 = {11, 12, 13, 14}};
    const zx_packet_user_t data2{.u64 = {21, 22, 23, 24}};
    const zx_packet_user_t data3{.u64 = {31, 32, 33, 34}};
    const zx_packet_user_t data_default{};

    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

    TestReceiver receiver1;
    TestReceiver receiver2;
    TestReceiver receiver3;

    EXPECT_EQ(ZX_OK, receiver1.QueuePacket(loop.dispatcher(), &data1), "queue 1");
    EXPECT_EQ(ZX_OK, receiver1.QueuePacket(loop.dispatcher(), &data3), "queue 1, again");
    EXPECT_EQ(ZX_OK, receiver2.QueuePacket(loop.dispatcher(), &data2), "queue 2");
    EXPECT_EQ(ZX_OK, receiver3.QueuePacket(loop.dispatcher(), nullptr), "queue 3");

    EXPECT_EQ(ZX_OK, loop.RunUntilIdle(), "run loop");
    EXPECT_EQ(2u, receiver1.run_count, "run count 1");
    EXPECT_EQ(ZX_OK, receiver1.last_status, "status 1");
    EXPECT_NONNULL(receiver1.last_data);
    EXPECT_EQ(0, memcmp(&data3, receiver1.last_data, sizeof(zx_packet_user_t)), "data 1");
    EXPECT_EQ(1u, receiver2.run_count, "run count 2");
    EXPECT_EQ(ZX_OK, receiver2.last_status, "status 2");
    EXPECT_NONNULL(receiver2.last_data);
    EXPECT_EQ(0, memcmp(&data2, receiver2.last_data, sizeof(zx_packet_user_t)), "data 2");
    EXPECT_EQ(1u, receiver3.run_count, "run count 3");
    EXPECT_EQ(ZX_OK, receiver3.last_status, "status 3");
    EXPECT_NONNULL(receiver3.last_data);
    EXPECT_EQ(0, memcmp(&data_default, receiver3.last_data, sizeof(zx_packet_user_t)), "data 3");

    END_TEST;
}

bool receiver_shutdown_test() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    loop.Shutdown();

    // Try to add work after shutdown.
    TestReceiver receiver;
    EXPECT_EQ(ZX_ERR_BAD_STATE, receiver.QueuePacket(loop.dispatcher(), nullptr), "queue after shutdown");
    EXPECT_EQ(0u, receiver.run_count, "run count 1");

    END_TEST;
}

class GetDefaultDispatcherTask : public QuitTask {
public:
    async_dispatcher_t* last_default_dispatcher;

protected:
    void Handle(async_dispatcher_t* dispatcher, zx_status_t status) override {
        QuitTask::Handle(dispatcher, status);
        last_default_dispatcher = async_get_default_dispatcher();
    }
};

class ConcurrencyMeasure {
public:
    ConcurrencyMeasure(uint32_t end)
        : end_(end) {}

    uint32_t max_threads() const { return fbl::atomic_load(&max_threads_, fbl::memory_order_acquire); }
    uint32_t count() const { return fbl::atomic_load(&count_, fbl::memory_order_acquire); }

    void Tally(async_dispatcher_t* dispatcher) {
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
        zx::nanosleep(zx::deadline_after(zx::msec(1)));

        // Decrement count of active threads.
        fbl::atomic_fetch_sub(&active_threads_, 1u, fbl::memory_order_acq_rel);

        // Quit when last item processed.
        if (1u + fbl::atomic_fetch_add(&count_, 1u, fbl::memory_order_acq_rel) == end_)
            async_loop_quit(async_loop_from_dispatcher(dispatcher));
    }

private:
    const uint32_t end_;
    fbl::atomic_uint32_t count_{};
    fbl::atomic_uint32_t active_threads_{};
    fbl::atomic_uint32_t max_threads_{};
};

class ThreadAssertWait : public TestWait {
public:
    ThreadAssertWait(zx_handle_t object, zx_signals_t trigger, ConcurrencyMeasure* measure)
        : TestWait(object, trigger), measure_(measure) {}

protected:
    ConcurrencyMeasure* measure_;

    void Handle(async_dispatcher_t* dispatcher, zx_status_t status,
                const zx_packet_signal_t* signal) override {
        TestWait::Handle(dispatcher, status, signal);
        measure_->Tally(dispatcher);
    }
};

class ThreadAssertTask : public TestTask {
public:
    ThreadAssertTask(ConcurrencyMeasure* measure)
        : measure_(measure) {}

protected:
    ConcurrencyMeasure* measure_;

    void Handle(async_dispatcher_t* dispatcher, zx_status_t status) override {
        TestTask::Handle(dispatcher, status);
        measure_->Tally(dispatcher);
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

    void Handle(async_dispatcher_t* dispatcher, zx_status_t status,
                const zx_packet_user_t* data) override {
        {
            fbl::AutoLock lock(&mutex_);
            TestReceiver::Handle(dispatcher, status, data);
        }
        measure_->Tally(dispatcher);
    }
};

bool threads_have_default_dispatcher() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    EXPECT_EQ(ZX_OK, loop.StartThread(), "start thread");

    GetDefaultDispatcherTask task;
    EXPECT_EQ(ZX_OK, task.Post(loop.dispatcher()), "post task");
    loop.JoinThreads();

    EXPECT_EQ(1u, task.run_count, "run count");
    EXPECT_EQ(ZX_OK, task.last_status, "status");
    EXPECT_EQ(loop.dispatcher(), task.last_default_dispatcher, "default dispatcher");

    END_TEST;
}

// The goal here is to ensure that threads stop when Quit() is called.
bool threads_quit() {
    const size_t num_threads = 4;

    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    for (size_t i = 0; i < num_threads; i++) {
        EXPECT_EQ(ZX_OK, loop.StartThread());
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

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    for (size_t i = 0; i < num_threads; i++) {
        EXPECT_EQ(ZX_OK, loop.StartThread());
    }
    loop.Shutdown();
    EXPECT_EQ(ASYNC_LOOP_SHUTDOWN, loop.GetState());

    loop.JoinThreads(); // should be a no-op

    EXPECT_EQ(ZX_ERR_BAD_STATE, loop.StartThread(), "can't start threads after shutdown");

    END_TEST;
}

// The goal here is to schedule a lot of work and see whether it runs
// on as many threads as we expected it to.
bool threads_waits_run_concurrently_test() {
    const size_t num_threads = 4;
    const size_t num_items = 100;

    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    for (size_t i = 0; i < num_threads; i++) {
        EXPECT_EQ(ZX_OK, loop.StartThread(), "start thread");
    }

    ConcurrencyMeasure measure(num_items);
    zx::event event;
    EXPECT_EQ(ZX_OK, zx::event::create(0u, &event), "create event");
    EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0), "signal");

    // Post a number of work items to run all at once.
    ThreadAssertWait* items[num_items];
    for (size_t i = 0; i < num_items; i++) {
        items[i] = new ThreadAssertWait(event.get(), ZX_USER_SIGNAL_0, &measure);
        EXPECT_EQ(ZX_OK, items[i]->Begin(loop.dispatcher()), "begin wait");
    }

    // Wait until quitted.
    loop.JoinThreads();

    // Ensure all work items completed.
    EXPECT_EQ(num_items, measure.count(), "item count");
    for (size_t i = 0; i < num_items; i++) {
        EXPECT_EQ(1u, items[i]->run_count, "run count");
        EXPECT_EQ(ZX_OK, items[i]->last_status, "status");
        EXPECT_NONNULL(items[i]->last_signal, "signal");
        EXPECT_EQ(ZX_USER_SIGNAL_0, items[i]->last_signal->observed & ZX_USER_SIGNAL_ALL, "observed");
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

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    for (size_t i = 0; i < num_threads; i++) {
        EXPECT_EQ(ZX_OK, loop.StartThread(), "start thread");
    }

    ConcurrencyMeasure measure(num_items);

    // Post a number of work items to run all at once.
    ThreadAssertTask* items[num_items];
    zx::time start_time = async::Now(loop.dispatcher());
    for (size_t i = 0; i < num_items; i++) {
        items[i] = new ThreadAssertTask(&measure);
        EXPECT_EQ(ZX_OK, items[i]->PostForTime(loop.dispatcher(), start_time + zx::msec(i)), "post task");
    }

    // Wait until quitted.
    loop.JoinThreads();

    // Ensure all work items completed.
    EXPECT_EQ(num_items, measure.count(), "item count");
    for (size_t i = 0; i < num_items; i++) {
        EXPECT_EQ(1u, items[i]->run_count, "run count");
        EXPECT_EQ(ZX_OK, items[i]->last_status, "status");
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

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    for (size_t i = 0; i < num_threads; i++) {
        EXPECT_EQ(ZX_OK, loop.StartThread(), "start thread");
    }

    ConcurrencyMeasure measure(num_items);

    // Post a number of packets all at once.
    ThreadAssertReceiver receiver(&measure);
    for (size_t i = 0; i < num_items; i++) {
        EXPECT_EQ(ZX_OK, receiver.QueuePacket(loop.dispatcher(), nullptr), "queue packet");
    }

    // Wait until quitted.
    loop.JoinThreads();

    // Ensure all work items completed.
    EXPECT_EQ(num_items, measure.count(), "item count");
    EXPECT_EQ(num_items, receiver.run_count, "run count");
    EXPECT_EQ(ZX_OK, receiver.last_status, "status");

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
RUN_TEST(time_test)
RUN_TEST(wait_test)
RUN_TEST(wait_unwaitable_handle_test)
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
