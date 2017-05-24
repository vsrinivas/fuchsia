// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <mx/handle.h>

#include <async/async.h>
#include <unittest/unittest.h>

#include "async_stub.h"

namespace {

class MockAsync : public AsyncStub {
public:
    enum class Op {
        NONE,
        BEGIN_WAIT,
        CANCEL_WAIT,
        POST_TASK,
        CANCEL_TASK,
        QUEUE_PACKET
    };

    Op last_op = Op::NONE;
    async_wait_t* last_wait = nullptr;
    async_task_t* last_task = nullptr;
    async_receiver_t* last_receiver = nullptr;
    const mx_packet_user_t* last_data = nullptr;

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

    mx_status_t PostTask(async_task_t* task) override {
        last_op = Op::POST_TASK;
        last_task = task;
        return MX_OK;
    }

    mx_status_t CancelTask(async_task_t* task) override {
        last_op = Op::CANCEL_TASK;
        last_task = task;
        return MX_OK;
    }

    mx_status_t QueuePacket(async_receiver_t* receiver,
                            const mx_packet_user_t* data) override {
        last_op = Op::QUEUE_PACKET;
        last_receiver = receiver;
        last_data = data;
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

bool wait_test() {
    const mx_handle_t dummy_handle = 1;
    const mx_signals_t dummy_trigger = MX_USER_SIGNAL_0;
    const mx_packet_signal_t dummy_signal{
        .trigger = dummy_trigger,
        .observed = MX_USER_SIGNAL_0 | MX_USER_SIGNAL_1,
        .count = 0u};
    const uint32_t dummy_flags = ASYNC_HANDLE_SHUTDOWN;

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

class MockTask : public async::Task {
public:
    MockTask() {}
    MockTask(mx_time_t deadline, uint32_t flags)
        : async::Task(deadline, flags) {}

    bool handler_ran = false;
    mx_status_t last_status = MX_ERR_INTERNAL;

protected:
    async_task_result_t Handle(async_t* async, mx_status_t status) override {
        handler_ran = true;
        last_status = status;
        return ASYNC_TASK_REPEAT;
    }
};

bool task_test() {
    const mx_time_t dummy_deadline = 1;
    const uint32_t dummy_flags = ASYNC_HANDLE_SHUTDOWN;

    BEGIN_TEST;

    MockTask default_task;
    EXPECT_EQ(MX_TIME_INFINITE, default_task.deadline(), "default deadline");
    EXPECT_EQ(0u, default_task.flags(), "default flags");

    default_task.set_deadline(dummy_deadline);
    EXPECT_EQ(dummy_deadline, default_task.deadline(), "set deadline");
    default_task.set_flags(dummy_flags);
    EXPECT_EQ(dummy_flags, default_task.flags(), "set flags");

    MockTask explicit_task(dummy_deadline, dummy_flags);
    EXPECT_EQ(dummy_deadline, default_task.deadline(), "explicit deadline");
    EXPECT_EQ(dummy_flags, explicit_task.flags(), "explicit flags");

    MockAsync async;
    EXPECT_EQ(MX_OK, explicit_task.Post(&async), "post, valid args");
    EXPECT_EQ(MockAsync::Op::POST_TASK, async.last_op, "op");
    EXPECT_EQ(dummy_deadline, async.last_task->deadline, "deadline");
    EXPECT_EQ(dummy_flags, async.last_task->flags, "flags");

    EXPECT_EQ(ASYNC_TASK_REPEAT, async.last_task->handler(&async, async.last_task, MX_OK),
              "invoke handler");
    EXPECT_TRUE(explicit_task.handler_ran, "handler ran");
    EXPECT_EQ(MX_OK, explicit_task.last_status, "status");

    EXPECT_EQ(MX_OK, explicit_task.Cancel(&async), "cancel, valid args");
    EXPECT_EQ(MockAsync::Op::CANCEL_TASK, async.last_op, "op");

    END_TEST;
}

class MockReceiver : public async::Receiver {
public:
    MockReceiver() {}
    MockReceiver(uint32_t flags)
        : async::Receiver(flags) {}

    bool handler_ran = false;
    mx_status_t last_status = MX_ERR_INTERNAL;
    const mx_packet_user_t* last_data = nullptr;

protected:
    void Handle(async_t* async, mx_status_t status, const mx_packet_user_t* data) override {
        handler_ran = true;
        last_status = status;
        last_data = data;
    }
};

bool receiver_test() {
    const uint32_t dummy_flags = ASYNC_HANDLE_SHUTDOWN;
    const mx_packet_user_t dummy_data{};

    BEGIN_TEST;

    MockReceiver default_receiver;
    EXPECT_EQ(0u, default_receiver.flags(), "default flags");

    default_receiver.set_flags(dummy_flags);
    EXPECT_EQ(dummy_flags, default_receiver.flags(), "set flags");

    MockReceiver explicit_receiver(dummy_flags);
    EXPECT_EQ(dummy_flags, explicit_receiver.flags(), "explicit flags");

    MockAsync async;
    EXPECT_EQ(MX_OK, explicit_receiver.Queue(&async, nullptr), "queue, null data");
    EXPECT_EQ(MockAsync::Op::QUEUE_PACKET, async.last_op, "op");
    EXPECT_EQ(dummy_flags, async.last_receiver->flags, "flags");
    EXPECT_NULL(async.last_data, "data");

    EXPECT_EQ(MX_OK, explicit_receiver.Queue(&async, &dummy_data), "queue, non-null data");
    EXPECT_EQ(MockAsync::Op::QUEUE_PACKET, async.last_op, "op");
    EXPECT_EQ(dummy_flags, async.last_receiver->flags, "flags");
    EXPECT_EQ(&dummy_data, async.last_data, "data");

    async.last_receiver->handler(&async, async.last_receiver, MX_OK, nullptr);
    EXPECT_TRUE(explicit_receiver.handler_ran, "handler ran");
    EXPECT_EQ(MX_OK, explicit_receiver.last_status, "status");
    EXPECT_NULL(explicit_receiver.last_data, "data");

    async.last_receiver->handler(&async, async.last_receiver, MX_OK, &dummy_data);
    EXPECT_TRUE(explicit_receiver.handler_ran, "handler ran");
    EXPECT_EQ(MX_OK, explicit_receiver.last_status, "status");
    EXPECT_EQ(&dummy_data, explicit_receiver.last_data, "data");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(async_wrapper_tests)
RUN_TEST(wait_test)
RUN_TEST(task_test)
RUN_TEST(receiver_test)
END_TEST_CASE(async_wrapper_tests)
