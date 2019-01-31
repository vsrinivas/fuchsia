// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/receiver.h>

#include <lib/async-testutils/dispatcher_stub.h>
#include <unittest/unittest.h>

namespace {

class MockDispatcher : public async::DispatcherStub {
public:
    enum class Op {
        NONE,
        QUEUE_PACKET
    };

    zx_status_t QueuePacket(async_receiver_t* receiver,
                            const zx_packet_user_t* data) override {
        last_op = Op::QUEUE_PACKET;
        last_receiver = receiver;
        last_data = data;
        return next_status;
    }

    Op last_op = Op::NONE;
    async_receiver_t* last_receiver = nullptr;
    const zx_packet_user_t* last_data = nullptr;
    zx_status_t next_status = ZX_OK;
};

class Harness {
public:
    Harness() { Reset(); }

    void Reset() {
        handler_ran = false;
        last_receiver = nullptr;
        last_status = ZX_ERR_INTERNAL;
        last_data = nullptr;
    }

    void Handler(async_dispatcher_t* dispatcher, async::ReceiverBase* receiver,
                 zx_status_t status, const zx_packet_user_t* data) {
        handler_ran = true;
        last_receiver = receiver;
        last_status = status;
        last_data = data;
    }

    virtual async::ReceiverBase& receiver() = 0;

    bool handler_ran;
    async::ReceiverBase* last_receiver;
    zx_status_t last_status;
    const zx_packet_user_t* last_data;
};

class LambdaHarness : public Harness {
public:
    async::ReceiverBase& receiver() override { return receiver_; }

private:
    async::Receiver receiver_{[this](async_dispatcher_t* dispatcher, async::Receiver* receiver,
                                     zx_status_t status, const zx_packet_user_t* data) {
        Handler(dispatcher, receiver, status, data);
    }};
};

class MethodHarness : public Harness {
public:
    async::ReceiverBase& receiver() override { return receiver_; }

private:
    async::ReceiverMethod<Harness, &Harness::Handler> receiver_{this};
};

bool receiver_set_handler_test() {
    BEGIN_TEST;

    {
        async::Receiver receiver;
        EXPECT_FALSE(receiver.has_handler());

        receiver.set_handler([](async_dispatcher_t* dispatcher, async::Receiver* receiver,
                                zx_status_t status, const zx_packet_user_t* data) {});
        EXPECT_TRUE(receiver.has_handler());
    }

    {
        async::Receiver receiver([](async_dispatcher_t* dispatcher, async::Receiver* receiver,
                                    zx_status_t status, const zx_packet_user_t* data) {});
        EXPECT_TRUE(receiver.has_handler());
    }

    END_TEST;
}

template <typename Harness>
bool receiver_queue_packet_test() {
    BEGIN_TEST;

    const zx_packet_user_t dummy_data{};
    MockDispatcher dispatcher;
    Harness harness;

    harness.Reset();
    dispatcher.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, harness.receiver().QueuePacket(&dispatcher, nullptr), "queue, null data");
    EXPECT_EQ(MockDispatcher::Op::QUEUE_PACKET, dispatcher.last_op);
    EXPECT_NULL(dispatcher.last_data);
    EXPECT_FALSE(harness.handler_ran);

    harness.Reset();
    dispatcher.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, harness.receiver().QueuePacket(&dispatcher, nullptr), "queue, null data");
    EXPECT_EQ(MockDispatcher::Op::QUEUE_PACKET, dispatcher.last_op);
    EXPECT_NULL(dispatcher.last_data);
    EXPECT_FALSE(harness.handler_ran);

    harness.Reset();
    dispatcher.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, harness.receiver().QueuePacket(&dispatcher, &dummy_data), "queue, non-null data");
    EXPECT_EQ(MockDispatcher::Op::QUEUE_PACKET, dispatcher.last_op);
    EXPECT_EQ(&dummy_data, dispatcher.last_data);
    EXPECT_FALSE(harness.handler_ran);

    harness.Reset();
    dispatcher.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, harness.receiver().QueuePacket(&dispatcher, &dummy_data), "queue, non-null data");
    EXPECT_EQ(MockDispatcher::Op::QUEUE_PACKET, dispatcher.last_op);
    EXPECT_EQ(&dummy_data, dispatcher.last_data);
    EXPECT_FALSE(harness.handler_ran);

    END_TEST;
}

template <typename Harness>
bool receiver_run_handler_test() {
    BEGIN_TEST;

    const zx_packet_user_t dummy_data{};
    MockDispatcher dispatcher;
    Harness harness;

    EXPECT_EQ(ZX_OK, harness.receiver().QueuePacket(&dispatcher, nullptr));
    EXPECT_EQ(ZX_OK, harness.receiver().QueuePacket(&dispatcher, &dummy_data));

    harness.Reset();
    dispatcher.last_receiver->handler(&dispatcher, dispatcher.last_receiver, ZX_OK, nullptr);
    EXPECT_TRUE(harness.handler_ran);
    EXPECT_EQ(&harness.receiver(), harness.last_receiver);
    EXPECT_EQ(ZX_OK, harness.last_status);
    EXPECT_NULL(harness.last_data);

    harness.Reset();
    dispatcher.last_receiver->handler(&dispatcher, dispatcher.last_receiver, ZX_OK, &dummy_data);
    EXPECT_TRUE(harness.handler_ran);
    EXPECT_EQ(&harness.receiver(), harness.last_receiver);
    EXPECT_EQ(ZX_OK, harness.last_status);
    EXPECT_EQ(&dummy_data, harness.last_data);

    END_TEST;
}

bool unsupported_queue_packet_test() {
    BEGIN_TEST;

    async::DispatcherStub dispatcher;
    async_receiver_t receiver{};
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_queue_packet(&dispatcher, &receiver, nullptr), "valid args without data");
    zx_packet_user_t data;
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_queue_packet(&dispatcher, &receiver, &data), "valid args with data");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(receiver_tests)
RUN_TEST(receiver_set_handler_test)
RUN_TEST((receiver_queue_packet_test<LambdaHarness>))
RUN_TEST((receiver_queue_packet_test<MethodHarness>))
RUN_TEST((receiver_run_handler_test<LambdaHarness>))
RUN_TEST((receiver_run_handler_test<MethodHarness>))
RUN_TEST(unsupported_queue_packet_test)
END_TEST_CASE(receiver_tests)
