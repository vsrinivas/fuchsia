// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/receiver.h>

#include <lib/async-testutils/async_stub.h>
#include <unittest/unittest.h>

namespace {

class MockAsync : public async::AsyncStub {
public:
    enum class Op {
        NONE,
        QUEUE_PACKET
    };

    Op last_op = Op::NONE;
    async_receiver_t* last_receiver = nullptr;
    const zx_packet_user_t* last_data = nullptr;
    zx_status_t next_status = ZX_OK;

    zx_status_t QueuePacket(async_receiver_t* receiver,
                            const zx_packet_user_t* data) override {
        last_op = Op::QUEUE_PACKET;
        last_receiver = receiver;
        last_data = data;
        return next_status;
    }
};

struct Handler {
    bool handler_ran;
    async::Receiver* last_receiver;
    zx_status_t last_status;
    const zx_packet_user_t* last_data;

    Handler() { Reset(); }

    void Reset() {
        handler_ran = false;
        last_receiver = nullptr;
        last_status = ZX_ERR_INTERNAL;
        last_data = nullptr;
    }

    async::Receiver::Handler MakeCallback() {
        return [this](async_t* async, async::Receiver* receiver,
                      zx_status_t status, const zx_packet_user_t* data) {
            handler_ran = true;
            last_receiver = receiver;
            last_status = status;
            last_data = data;
        };
    }
};

bool constructors() {
    BEGIN_TEST;

    Handler handler;

    {
        async::Receiver receiver;
        EXPECT_FALSE(!!receiver.handler());

        receiver.set_handler(handler.MakeCallback());
        EXPECT_TRUE(!!receiver.handler());
    }

    {
        async::Receiver receiver(handler.MakeCallback());
        EXPECT_TRUE(!!receiver.handler());
    }

    END_TEST;
}

bool queue_packet_test() {
    BEGIN_TEST;

    const zx_packet_user_t dummy_data{};
    Handler handler;
    MockAsync async;
    async::Receiver receiver(handler.MakeCallback());

    handler.Reset();
    async.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, receiver.QueuePacket(&async, nullptr), "queue, null data");
    EXPECT_EQ(MockAsync::Op::QUEUE_PACKET, async.last_op);
    EXPECT_NULL(async.last_data);
    EXPECT_FALSE(handler.handler_ran);

    handler.Reset();
    async.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, receiver.QueuePacket(&async, nullptr), "queue, null data");
    EXPECT_EQ(MockAsync::Op::QUEUE_PACKET, async.last_op);
    EXPECT_NULL(async.last_data);
    EXPECT_FALSE(handler.handler_ran);

    handler.Reset();
    async.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, receiver.QueuePacket(&async, &dummy_data), "queue, non-null data");
    EXPECT_EQ(MockAsync::Op::QUEUE_PACKET, async.last_op);
    EXPECT_EQ(&dummy_data, async.last_data);
    EXPECT_FALSE(handler.handler_ran);

    handler.Reset();
    async.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, receiver.QueuePacket(&async, &dummy_data), "queue, non-null data");
    EXPECT_EQ(MockAsync::Op::QUEUE_PACKET, async.last_op);
    EXPECT_EQ(&dummy_data, async.last_data);
    EXPECT_FALSE(handler.handler_ran);

    END_TEST;
}

bool queue_packet_or_report_error_test() {
    BEGIN_TEST;

    const zx_packet_user_t dummy_data{};
    Handler handler;
    MockAsync async;
    async::Receiver receiver(handler.MakeCallback());

    handler.Reset();
    async.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, receiver.QueuePacketOrReportError(&async, nullptr), "queue, null data");
    EXPECT_EQ(MockAsync::Op::QUEUE_PACKET, async.last_op);
    EXPECT_NULL(async.last_data);
    EXPECT_FALSE(handler.handler_ran);

    handler.Reset();
    async.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, receiver.QueuePacketOrReportError(&async, nullptr), "queue, null data");
    EXPECT_EQ(MockAsync::Op::QUEUE_PACKET, async.last_op);
    EXPECT_NULL(async.last_data);
    EXPECT_TRUE(handler.handler_ran);
    EXPECT_EQ(&receiver, handler.last_receiver);
    EXPECT_EQ(ZX_ERR_BAD_STATE, handler.last_status);
    EXPECT_NULL(handler.last_data);

    handler.Reset();
    async.next_status = ZX_OK;
    EXPECT_EQ(ZX_OK, receiver.QueuePacketOrReportError(&async, &dummy_data), "queue, non-null data");
    EXPECT_EQ(MockAsync::Op::QUEUE_PACKET, async.last_op);
    EXPECT_EQ(&dummy_data, async.last_data);
    EXPECT_FALSE(handler.handler_ran);

    handler.Reset();
    async.next_status = ZX_ERR_BAD_STATE;
    EXPECT_EQ(ZX_ERR_BAD_STATE, receiver.QueuePacketOrReportError(&async, &dummy_data), "queue, non-null data");
    EXPECT_EQ(MockAsync::Op::QUEUE_PACKET, async.last_op);
    EXPECT_EQ(&dummy_data, async.last_data);
    EXPECT_TRUE(handler.handler_ran);
    EXPECT_EQ(&receiver, handler.last_receiver);
    EXPECT_EQ(ZX_ERR_BAD_STATE, handler.last_status);
    EXPECT_NULL(handler.last_data);

    END_TEST;
}

bool run_receiver_test() {
    BEGIN_TEST;

    const zx_packet_user_t dummy_data{};
    Handler handler;
    MockAsync async;
    async::Receiver receiver(handler.MakeCallback());

    EXPECT_EQ(ZX_OK, receiver.QueuePacket(&async, nullptr));
    EXPECT_EQ(ZX_OK, receiver.QueuePacket(&async, &dummy_data));

    handler.Reset();
    async.last_receiver->handler(&async, async.last_receiver, ZX_OK, nullptr);
    EXPECT_TRUE(handler.handler_ran);
    EXPECT_EQ(&receiver, handler.last_receiver);
    EXPECT_EQ(ZX_OK, handler.last_status);
    EXPECT_NULL(handler.last_data);

    handler.Reset();
    async.last_receiver->handler(&async, async.last_receiver, ZX_OK, &dummy_data);
    EXPECT_TRUE(handler.handler_ran);
    EXPECT_EQ(&receiver, handler.last_receiver);
    EXPECT_EQ(ZX_OK, handler.last_status);
    EXPECT_EQ(&dummy_data, handler.last_data);

    END_TEST;
}

bool unsupported_queue_packet_test() {
    BEGIN_TEST;

    async::AsyncStub async;
    async_receiver_t receiver{};
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_queue_packet(&async, &receiver, nullptr), "valid args without data");
    zx_packet_user_t data;
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, async_queue_packet(&async, &receiver, &data), "valid args with data");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(receiver_tests)
RUN_TEST(constructors)
RUN_TEST(queue_packet_test)
RUN_TEST(queue_packet_or_report_error_test)
RUN_TEST(run_receiver_test)
RUN_TEST(unsupported_queue_packet_test)
END_TEST_CASE(receiver_tests)
