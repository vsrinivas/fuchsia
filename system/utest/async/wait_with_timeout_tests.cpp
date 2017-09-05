// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/wait_with_timeout.h>

#include <unittest/unittest.h>

#include "async_stub.h"

namespace {

class MockAsync : public AsyncStub {
public:
    async_wait_t* last_begin_wait = nullptr;
    async_wait_t* last_cancel_wait = nullptr;
    async_task_t* last_post_task = nullptr;
    async_task_t* last_cancel_task = nullptr;
    mx_status_t next_begin_wait_status = MX_OK;
    mx_status_t next_cancel_wait_status = MX_OK;
    mx_status_t next_post_task_status = MX_OK;
    mx_status_t next_cancel_task_status = MX_OK;

    mx_status_t BeginWait(async_wait_t* wait) override {
        last_begin_wait = wait;
        return next_begin_wait_status;
    }

    mx_status_t CancelWait(async_wait_t* wait) override {
        last_cancel_wait = wait;
        return next_cancel_wait_status;
    }

    mx_status_t PostTask(async_task_t* task) override {
        last_post_task = task;
        return next_post_task_status;
    }

    mx_status_t CancelTask(async_task_t* task) override {
        last_cancel_task = task;
        return next_cancel_task_status;
    }
};

struct Handler {
    Handler(async::WaitWithTimeout* wait) {
        wait->set_handler([this, wait](async_t* async, mx_status_t status,
                                       const mx_packet_signal_t* signal) {
            handler_ran = true;
            last_status = status;
            last_signal = signal;
            wait->set_deadline(wait->deadline() + 100u);
            return status == MX_OK ? ASYNC_WAIT_AGAIN : ASYNC_WAIT_FINISHED;
        });
    }

    bool handler_ran = false;
    mx_status_t last_status = MX_ERR_INTERNAL;
    const mx_packet_signal_t* last_signal = nullptr;
};

bool timeout_test() {
    const mx_handle_t dummy_handle = 1;
    const mx_signals_t dummy_trigger = MX_USER_SIGNAL_0;
    const mx_packet_signal_t dummy_signal{
        .trigger = dummy_trigger,
        .observed = MX_USER_SIGNAL_0 | MX_USER_SIGNAL_1,
        .count = 0u,
        .reserved0 = 0u,
        .reserved1 = 0u};
    const mx_time_t dummy_deadline = 100u;
    const uint32_t dummy_flags = ASYNC_FLAG_HANDLE_SHUTDOWN;

    BEGIN_TEST;

    {
        async::WaitWithTimeout default_wait;
        EXPECT_EQ(MX_HANDLE_INVALID, default_wait.object(), "default object");
        EXPECT_EQ(MX_SIGNAL_NONE, default_wait.trigger(), "default trigger");
        EXPECT_EQ(MX_TIME_INFINITE, default_wait.deadline(), "default deadline");
        EXPECT_EQ(0u, default_wait.flags(), "default flags");

        default_wait.set_object(dummy_handle);
        EXPECT_EQ(dummy_handle, default_wait.object(), "set object");
        default_wait.set_trigger(dummy_trigger);
        EXPECT_EQ(dummy_trigger, default_wait.trigger(), "set trigger");
        default_wait.set_deadline(dummy_deadline);
        EXPECT_EQ(dummy_deadline, default_wait.deadline(), "set deadline");
        default_wait.set_flags(dummy_flags);
        EXPECT_EQ(dummy_flags, default_wait.flags(), "set flags");

        EXPECT_FALSE(!!default_wait.handler(), "handler");

        // Begin waiting without timeout (will be canceled immediately).
        MockAsync async;
        default_wait.set_deadline(MX_TIME_INFINITE);
        EXPECT_EQ(MX_OK, default_wait.Begin(&async), "begin, valid args");
        EXPECT_NONNULL(async.last_begin_wait, "begin wait called");
        EXPECT_NULL(async.last_post_task, "post task not called");
        EXPECT_EQ(dummy_handle, async.last_begin_wait->object, "handle");
        EXPECT_EQ(dummy_trigger, async.last_begin_wait->trigger, "trigger");
        EXPECT_EQ(dummy_flags, async.last_begin_wait->flags, "flags");
        async.last_begin_wait = nullptr;

        // Cancel waiting without timeout.
        EXPECT_EQ(MX_OK, default_wait.Cancel(&async), "cancel, valid args");
        EXPECT_NONNULL(async.last_cancel_wait, "cancel wait called");
        EXPECT_NULL(async.last_cancel_task, "cancel task not called");
        async.last_cancel_wait = nullptr;
    }

    {
        async::WaitWithTimeout explicit_wait(dummy_handle, dummy_trigger, dummy_deadline, dummy_flags);
        EXPECT_EQ(dummy_handle, explicit_wait.object(), "explicit object");
        EXPECT_EQ(dummy_trigger, explicit_wait.trigger(), "explicit trigger");
        EXPECT_EQ(dummy_deadline, explicit_wait.deadline(), "explicit deadline");
        EXPECT_EQ(dummy_flags, explicit_wait.flags(), "explicit flags");

        EXPECT_FALSE(!!explicit_wait.handler(), "handler");
        Handler handler(&explicit_wait);
        EXPECT_TRUE(!!explicit_wait.handler());

        // Begin waiting without timeout.
        MockAsync async;
        EXPECT_EQ(MX_OK, explicit_wait.Begin(&async), "begin, valid args");
        EXPECT_NONNULL(async.last_begin_wait, "begin wait called");
        EXPECT_NONNULL(async.last_post_task, "post task called");
        EXPECT_EQ(dummy_handle, async.last_begin_wait->object, "handle");
        EXPECT_EQ(dummy_trigger, async.last_begin_wait->trigger, "trigger");
        EXPECT_EQ(dummy_flags, async.last_begin_wait->flags, "flags");
        EXPECT_EQ(dummy_deadline, async.last_post_task->deadline, "deadline");
        async_wait_t* wait_context = async.last_begin_wait;
        async_task_t* task_context = async.last_post_task;
        async.last_begin_wait = nullptr;
        async.last_post_task = nullptr;

        // Handle wait.
        EXPECT_EQ(ASYNC_WAIT_AGAIN, wait_context->handler(&async, wait_context, MX_OK, &dummy_signal),
                  "invoke wait handler");
        EXPECT_TRUE(handler.handler_ran, "handler ran");
        EXPECT_EQ(MX_OK, handler.last_status, "status");
        EXPECT_EQ(&dummy_signal, handler.last_signal, "signal");
        EXPECT_NONNULL(async.last_cancel_task, "cancel task called");
        EXPECT_NONNULL(async.last_post_task, "post task called");
        EXPECT_EQ(dummy_deadline + 100u, async.last_post_task->deadline, "deadline");
        handler.handler_ran = false;
        async.last_cancel_task = nullptr;
        async.last_post_task = nullptr;

        // Handle timeout.
        EXPECT_EQ(ASYNC_TASK_FINISHED, task_context->handler(&async, task_context, MX_OK),
                  "invoke timeout handler");
        EXPECT_TRUE(handler.handler_ran, "handler ran");
        EXPECT_EQ(MX_ERR_TIMED_OUT, handler.last_status, "status");
        EXPECT_NULL(handler.last_signal, "signal");
        handler.handler_ran = false;

        // Cancel waiting with timeout.
        EXPECT_EQ(MX_OK, explicit_wait.Cancel(&async), "cancel, valid args");
        EXPECT_NONNULL(async.last_cancel_wait, "cancel wait called");
        EXPECT_NONNULL(async.last_cancel_task, "cancel task called");
    }

    END_TEST;
}

bool begin_wait_cleans_up() {
    const mx_handle_t dummy_handle = 1;
    const mx_signals_t dummy_trigger = MX_USER_SIGNAL_0;
    const mx_time_t dummy_deadline = 100u;
    const uint32_t dummy_flags = ASYNC_FLAG_HANDLE_SHUTDOWN;

    BEGIN_TEST;

    async::WaitWithTimeout wait(dummy_handle, dummy_trigger, dummy_deadline, dummy_flags);

    // If an error occurs while setting the timeout, cancel the wait.
    MockAsync async;
    async.next_post_task_status = MX_ERR_BAD_STATE;
    EXPECT_EQ(MX_ERR_BAD_STATE, wait.Begin(&async), "begin, will fail to post task");
    EXPECT_NONNULL(async.last_begin_wait, "begin wait called");
    EXPECT_NONNULL(async.last_post_task, "post task called");
    EXPECT_NONNULL(async.last_cancel_wait, "cancel wait called");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(wait_with_timeout_tests)
RUN_TEST(timeout_test)
RUN_TEST(begin_wait_cleans_up)
END_TEST_CASE(wait_with_timeout_tests)
