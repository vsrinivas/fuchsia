// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

class MockWait : public async::Wait {
public:
    MockWait() {}
    MockWait(mx_handle_t object, mx_signals_t trigger, uint32_t flags)
        : async::Wait(object, trigger, flags) {}

    bool handler_ran = false;
    mx_status_t last_status = MX_ERR_INTERNAL;
    const mx_packet_signal_t* last_signal = nullptr;

protected:
    async_wait_result_t Handle(async_t* async, mx_status_t status,
                               const mx_packet_signal_t* signal) override {
        handler_ran = true;
        last_status = status;
        last_signal = signal;
        return ASYNC_WAIT_AGAIN;
    }
};

bool wrapper_test() {
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

    MockWait default_wait;
    EXPECT_EQ(MX_HANDLE_INVALID, default_wait.object(), "default object");
    EXPECT_EQ(MX_SIGNAL_NONE, default_wait.trigger(), "default trigger");
    EXPECT_EQ(0u, default_wait.flags(), "default flags");

    default_wait.set_object(dummy_handle);
    EXPECT_EQ(dummy_handle, default_wait.object(), "set object");
    default_wait.set_trigger(dummy_trigger);
    EXPECT_EQ(dummy_trigger, default_wait.trigger(), "set trigger");
    default_wait.set_flags(dummy_flags);
    EXPECT_EQ(dummy_flags, default_wait.flags(), "set flags");

    MockWait explicit_wait(dummy_handle, dummy_trigger, dummy_flags);
    EXPECT_EQ(dummy_handle, explicit_wait.object(), "explicit object");
    EXPECT_EQ(dummy_trigger, explicit_wait.trigger(), "explicit trigger");
    EXPECT_EQ(dummy_flags, explicit_wait.flags(), "explicit flags");

    MockAsync async;
    EXPECT_EQ(MX_OK, explicit_wait.Begin(&async), "begin, valid args");
    EXPECT_EQ(MockAsync::Op::BEGIN_WAIT, async.last_op, "op");
    EXPECT_EQ(dummy_handle, async.last_wait->object, "handle");
    EXPECT_EQ(dummy_trigger, async.last_wait->trigger, "trigger");
    EXPECT_EQ(dummy_flags, async.last_wait->flags, "flags");

    EXPECT_EQ(ASYNC_WAIT_AGAIN, async.last_wait->handler(&async, async.last_wait, MX_OK, &dummy_signal),
              "invoke handler");
    EXPECT_TRUE(explicit_wait.handler_ran, "handler ran");
    EXPECT_EQ(MX_OK, explicit_wait.last_status, "status");
    EXPECT_EQ(&dummy_signal, explicit_wait.last_signal, "signal");

    EXPECT_EQ(MX_OK, explicit_wait.Cancel(&async), "cancel, valid args");
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
RUN_TEST(wrapper_test)
RUN_TEST(unsupported_begin_wait_test)
RUN_TEST(unsupported_cancel_wait_test)
END_TEST_CASE(wait_tests)
