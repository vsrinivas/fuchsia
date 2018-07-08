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
        connection->SendNotification(test_buffer_ids_);
    }

    static void CallbackStatic(void* token, msd_notification_t* notification)
    {
        reinterpret_cast<TestMsdIntelConnection*>(token)->Callback(notification);
    }
    void Callback(msd_notification_t* notification)
    {
        EXPECT_EQ(MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND, notification->type);
        switch (callback_count_++) {
            case 0:
                EXPECT_EQ(64u, notification->u.channel_send.size);
                EXPECT_EQ(test_buffer_ids_[0],
                          reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[0]);
                EXPECT_EQ(test_buffer_ids_[1],
                          reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[1]);
                EXPECT_EQ(test_buffer_ids_[2],
                          reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[2]);
                EXPECT_EQ(test_buffer_ids_[3],
                          reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[3]);
                EXPECT_EQ(test_buffer_ids_[4],
                          reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[4]);
                EXPECT_EQ(test_buffer_ids_[5],
                          reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[5]);
                EXPECT_EQ(test_buffer_ids_[6],
                          reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[6]);
                EXPECT_EQ(test_buffer_ids_[7],
                          reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[7]);
                break;
            case 1:
                EXPECT_EQ(16u, notification->u.channel_send.size);
                EXPECT_EQ(test_buffer_ids_[8],
                          reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[0]);
                EXPECT_EQ(test_buffer_ids_[9],
                          reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[1]);
                break;
            default:
                EXPECT_TRUE(false);
        }
    }

private:
    MockBusMapper mock_bus_mapper_;
    std::vector<uint64_t> test_buffer_ids_{10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    uint32_t callback_count_ = 0;
};

TEST(MsdIntelConnection, Notification) { TestMsdIntelConnection().Notification(); }
