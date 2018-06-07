// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_bus_mapper.h"
#include "msd_intel_connection.h"
#include "gtest/gtest.h"

class TestMsdIntelConnection : public MsdIntelConnection::Owner {
public:
    magma::Status SubmitCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf) override
    {
        return MAGMA_STATUS_UNIMPLEMENTED;
    }

    void DestroyContext(std::shared_ptr<ClientContext> client_context) override {}
    void ReleaseBuffer(std::shared_ptr<AddressSpace> address_space,
                       std::shared_ptr<MsdIntelBuffer> buffer) override
    {
    }

    magma::PlatformBusMapper* GetBusMapper() override { return &mock_bus_mapper_; }

    void Notification()
    {
        auto connection = MsdIntelConnection::Create(this, 0);
        ASSERT_NE(connection, nullptr);

        connection->SetNotificationCallback(CallbackStatic, this);
        connection->SendNotification(test_buffer_id_);
    }

    static void CallbackStatic(void* token, msd_notification_t* notification)
    {
        reinterpret_cast<TestMsdIntelConnection*>(token)->Callback(notification);
    }
    void Callback(msd_notification_t* notification)
    {
        EXPECT_EQ(MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND, notification->type);
        EXPECT_EQ(test_buffer_id_, *reinterpret_cast<uint64_t*>(notification->u.channel_send.data));
        EXPECT_EQ(sizeof(test_buffer_id_), notification->u.channel_send.size);
    }

private:
    MockBusMapper mock_bus_mapper_;
    uint64_t test_buffer_id_ = 0xabab1234;
};

TEST(MsdIntelConnection, Notification) { TestMsdIntelConnection().Notification(); }
