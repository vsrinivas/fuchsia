// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/receiver.h>

#include <unittest/unittest.h>

#include "async_stub.h"

namespace {

class MockAsync : public AsyncStub {
public:
    enum class Op {
        NONE,
        QUEUE_PACKET
    };

    Op last_op = Op::NONE;
    async_receiver_t* last_receiver = nullptr;
    const mx_packet_user_t* last_data = nullptr;

    mx_status_t QueuePacket(async_receiver_t* receiver,
                            const mx_packet_user_t* data) override {
        last_op = Op::QUEUE_PACKET;
        last_receiver = receiver;
        last_data = data;
        return MX_OK;
    }
};

struct Handler {
    Handler(async::Receiver* receiver) {
        receiver->set_handler([this](async_t* async, mx_status_t status,
                                     const mx_packet_user_t* data) {
            handler_ran = true;
            last_status = status;
            last_data = data;
        });
    }

    bool handler_ran = false;
    mx_status_t last_status = MX_ERR_INTERNAL;
    const mx_packet_user_t* last_data = nullptr;
};

bool wrapper_test() {
    const uint32_t dummy_flags = ASYNC_FLAG_HANDLE_SHUTDOWN;
    const mx_packet_user_t dummy_data{};

    BEGIN_TEST;

    {
        async::Receiver default_receiver;
        EXPECT_EQ(0u, default_receiver.flags(), "default flags");

        default_receiver.set_flags(dummy_flags);
        EXPECT_EQ(dummy_flags, default_receiver.flags(), "set flags");

        EXPECT_FALSE(!!default_receiver.handler(), "handler");
    }

    {
        async::Receiver explicit_receiver(dummy_flags);
        EXPECT_EQ(dummy_flags, explicit_receiver.flags(), "explicit flags");

        EXPECT_FALSE(!!explicit_receiver.handler(), "handler");
        Handler handler(&explicit_receiver);
        EXPECT_TRUE(!!explicit_receiver.handler());

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
        EXPECT_TRUE(handler.handler_ran, "handler ran");
        EXPECT_EQ(MX_OK, handler.last_status, "status");
        EXPECT_NULL(handler.last_data, "data");

        async.last_receiver->handler(&async, async.last_receiver, MX_OK, &dummy_data);
        EXPECT_TRUE(handler.handler_ran, "handler ran");
        EXPECT_EQ(MX_OK, handler.last_status, "status");
        EXPECT_EQ(&dummy_data, handler.last_data, "data");
    }

    END_TEST;
}

bool unsupported_queue_packet_test() {
    BEGIN_TEST;

    AsyncStub async;
    async_receiver_t receiver{};
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_queue_packet(&async, &receiver, nullptr), "valid args without data");
    mx_packet_user_t data;
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_queue_packet(&async, &receiver, &data), "valid args with data");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(receiver_tests)
RUN_TEST(wrapper_test)
RUN_TEST(unsupported_queue_packet_test)
END_TEST_CASE(receiver_tests)
