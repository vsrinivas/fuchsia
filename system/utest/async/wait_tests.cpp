// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/auto_wait.h>
#include <async/wait.h>

#include <unittest/unittest.h>

#include "async_stub.h"

namespace {

class MockAsync : public AsyncStub {
public:
    enum class Op {
        NONE,
        BEGIN_WAIT,
        CANCEL_WAIT,
    };

    Op last_op = Op::NONE;
    async_wait_t* last_wait = nullptr;

    mx_status_t BeginWait(async_wait_t* wait) override {
        last_op = Op::BEGIN_WAIT;
        last_wait = wait;
        return MX_OK;
    }

    mx_status_t CancelWait(async_wait_t* wait) override {
        last_op = Op::CANCEL_WAIT;
        last_wait = wait;
        return MX_OK;
    }
};

template <typename TWait>
struct Handler {
    Handler(TWait* wait, async_wait_result_t result)
        : result(result) {
        wait->set_handler([this](async_t* async, mx_status_t status,
                                 const mx_packet_signal_t* signal) {
            handler_ran = true;
            last_status = status;
            last_signal = signal;
            return this->result;
        });
    }

    async_wait_result_t result;
    bool handler_ran = false;
    mx_status_t last_status = MX_ERR_INTERNAL;
    const mx_packet_signal_t* last_signal = nullptr;
};

bool wait_test() {
    const mx_handle_t dummy_handle = 1;
    const mx_signals_t dummy_trigger = MX_USER_SIGNAL_0;
    const mx_packet_signal_t dummy_signal{
        .trigger = dummy_trigger,
        .observed = MX_USER_SIGNAL_0 | MX_USER_SIGNAL_1,
        .count = 0u,
        .reserved0 = 0u,
        .reserved1 = 0u};
    const uint32_t dummy_flags = ASYNC_FLAG_HANDLE_SHUTDOWN;

    BEGIN_TEST;

    {
        async::Wait default_wait;
        EXPECT_EQ(MX_HANDLE_INVALID, default_wait.object(), "default object");
        EXPECT_EQ(MX_SIGNAL_NONE, default_wait.trigger(), "default trigger");
        EXPECT_EQ(0u, default_wait.flags(), "default flags");

        default_wait.set_object(dummy_handle);
        EXPECT_EQ(dummy_handle, default_wait.object(), "set object");
        default_wait.set_trigger(dummy_trigger);
        EXPECT_EQ(dummy_trigger, default_wait.trigger(), "set trigger");
        default_wait.set_flags(dummy_flags);
        EXPECT_EQ(dummy_flags, default_wait.flags(), "set flags");

        EXPECT_FALSE(!!default_wait.handler(), "handler");
    }

    {
        async::Wait explicit_wait(dummy_handle, dummy_trigger, dummy_flags);
        EXPECT_EQ(dummy_handle, explicit_wait.object(), "explicit object");
        EXPECT_EQ(dummy_trigger, explicit_wait.trigger(), "explicit trigger");
        EXPECT_EQ(dummy_flags, explicit_wait.flags(), "explicit flags");

        // begin a repeating wait
        EXPECT_FALSE(!!explicit_wait.handler(), "handler");
        Handler<async::Wait> handler(&explicit_wait, ASYNC_WAIT_AGAIN);
        EXPECT_TRUE(!!explicit_wait.handler());

        MockAsync async;
        EXPECT_EQ(MX_OK, explicit_wait.Begin(&async), "begin, valid args");
        EXPECT_EQ(MockAsync::Op::BEGIN_WAIT, async.last_op, "op");
        EXPECT_EQ(dummy_handle, async.last_wait->object, "handle");
        EXPECT_EQ(dummy_trigger, async.last_wait->trigger, "trigger");
        EXPECT_EQ(dummy_flags, async.last_wait->flags, "flags");

        EXPECT_EQ(ASYNC_WAIT_AGAIN,
                  async.last_wait->handler(&async, async.last_wait, MX_OK, &dummy_signal),
                  "invoke handler");
        EXPECT_TRUE(handler.handler_ran, "handler ran");
        EXPECT_EQ(MX_OK, handler.last_status, "status");
        EXPECT_EQ(&dummy_signal, handler.last_signal, "signal");

        // cancel the wait
        EXPECT_EQ(MX_OK, explicit_wait.Cancel(&async), "cancel, valid args");
        EXPECT_EQ(MockAsync::Op::CANCEL_WAIT, async.last_op, "op");
    }

    END_TEST;
}

bool auto_wait_test() {
    BEGIN_TEST;

    const mx_handle_t dummy_handle = 1;
    const mx_signals_t dummy_trigger = MX_USER_SIGNAL_0;
    const mx_packet_signal_t dummy_signal{
        .trigger = dummy_trigger,
        .observed = MX_USER_SIGNAL_0 | MX_USER_SIGNAL_1,
        .count = 0u,
        .reserved0 = 0u,
        .reserved1 = 0u};
    const uint32_t dummy_flags = ASYNC_FLAG_HANDLE_SHUTDOWN;

    BEGIN_TEST;

    MockAsync async;
    {
        async::AutoWait default_wait(&async);
        EXPECT_EQ(&async, default_wait.async());
        EXPECT_FALSE(default_wait.is_pending());
        EXPECT_EQ(MX_HANDLE_INVALID, default_wait.object(), "default object");
        EXPECT_EQ(MX_SIGNAL_NONE, default_wait.trigger(), "default trigger");
        EXPECT_EQ(0u, default_wait.flags(), "default flags");

        default_wait.set_object(dummy_handle);
        EXPECT_EQ(dummy_handle, default_wait.object(), "set object");
        default_wait.set_trigger(dummy_trigger);
        EXPECT_EQ(dummy_trigger, default_wait.trigger(), "set trigger");
        default_wait.set_flags(dummy_flags);
        EXPECT_EQ(dummy_flags, default_wait.flags(), "set flags");

        EXPECT_FALSE(!!default_wait.handler(), "handler");
    }
    EXPECT_EQ(MockAsync::Op::NONE, async.last_op, "op");

    {
        async::AutoWait explicit_wait(&async, dummy_handle, dummy_trigger, dummy_flags);
        EXPECT_EQ(&async, explicit_wait.async());
        EXPECT_FALSE(explicit_wait.is_pending());
        EXPECT_EQ(dummy_handle, explicit_wait.object(), "explicit object");
        EXPECT_EQ(dummy_trigger, explicit_wait.trigger(), "explicit trigger");
        EXPECT_EQ(dummy_flags, explicit_wait.flags(), "explicit flags");

        // begin a non-repeating wait
        EXPECT_FALSE(!!explicit_wait.handler(), "handler");
        Handler<async::AutoWait> handler(&explicit_wait, ASYNC_WAIT_FINISHED);
        EXPECT_TRUE(!!explicit_wait.handler());

        EXPECT_EQ(MX_OK, explicit_wait.Begin(), "begin, valid args");
        EXPECT_TRUE(explicit_wait.is_pending());
        EXPECT_EQ(MockAsync::Op::BEGIN_WAIT, async.last_op, "op");
        EXPECT_EQ(dummy_handle, async.last_wait->object, "handle");
        EXPECT_EQ(dummy_trigger, async.last_wait->trigger, "trigger");
        EXPECT_EQ(dummy_flags, async.last_wait->flags, "flags");

        EXPECT_EQ(ASYNC_WAIT_FINISHED,
                  async.last_wait->handler(&async, async.last_wait, MX_OK, &dummy_signal),
                  "invoke handler");
        EXPECT_FALSE(explicit_wait.is_pending());
        EXPECT_TRUE(handler.handler_ran, "handler ran");
        EXPECT_EQ(MX_OK, handler.last_status, "status");
        EXPECT_EQ(&dummy_signal, handler.last_signal, "signal");

        // begin a repeating wait
        handler.result = ASYNC_WAIT_AGAIN;

        EXPECT_EQ(MX_OK, explicit_wait.Begin(), "begin, valid args");
        EXPECT_TRUE(explicit_wait.is_pending());
        EXPECT_EQ(MockAsync::Op::BEGIN_WAIT, async.last_op, "op");
        EXPECT_EQ(dummy_handle, async.last_wait->object, "handle");
        EXPECT_EQ(dummy_trigger, async.last_wait->trigger, "trigger");
        EXPECT_EQ(dummy_flags, async.last_wait->flags, "flags");

        EXPECT_EQ(ASYNC_WAIT_AGAIN,
                  async.last_wait->handler(&async, async.last_wait, MX_OK, &dummy_signal),
                  "invoke handler");
        EXPECT_TRUE(explicit_wait.is_pending());
        EXPECT_TRUE(handler.handler_ran, "handler ran");
        EXPECT_EQ(MX_OK, handler.last_status, "status");
        EXPECT_EQ(&dummy_signal, handler.last_signal, "signal");

        // cancel the wait
        explicit_wait.Cancel();
        EXPECT_EQ(MockAsync::Op::CANCEL_WAIT, async.last_op, "op");
        EXPECT_FALSE(explicit_wait.is_pending());

        // begin the wait again then let it go out of scope
        EXPECT_EQ(MX_OK, explicit_wait.Begin(), "begin, valid args");
        EXPECT_TRUE(explicit_wait.is_pending());
        EXPECT_EQ(MockAsync::Op::BEGIN_WAIT, async.last_op, "op");
    }
    EXPECT_EQ(MockAsync::Op::CANCEL_WAIT, async.last_op, "op");

    END_TEST;
}

bool unsupported_begin_wait_test() {
    BEGIN_TEST;

    AsyncStub async;
    async_wait_t wait{};
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_begin_wait(&async, &wait), "valid args");

    END_TEST;
}

bool unsupported_cancel_wait_test() {
    BEGIN_TEST;

    AsyncStub async;
    async_wait_t wait{};
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_cancel_wait(&async, &wait), "valid args");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(wait_tests)
RUN_TEST(wait_test)
RUN_TEST(auto_wait_test)
RUN_TEST(unsupported_begin_wait_test)
RUN_TEST(unsupported_cancel_wait_test)
END_TEST_CASE(wait_tests)
