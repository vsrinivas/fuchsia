// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "gtest/gtest.h"

#include "gpu_mapping.h"
#include "msd_arm_buffer.h"
#include "msd_arm_connection.h"

namespace {

uint32_t g_test_channel;
uint64_t g_test_data_size;
magma_arm_mali_status g_status;

magma_status_t TestSendCallback(msd_channel_t channel, void* data, uint64_t data_size)
{
    g_test_channel = channel;
    g_test_data_size = data_size;
    memcpy(&g_status, data, data_size);
    return MAGMA_STATUS_OK;
}
}

class TestConnection {
public:
    void MapUnmap()
    {
        auto connection = MsdArmConnection::Create(0, nullptr);
        EXPECT_TRUE(connection);
        constexpr uint64_t kBufferSize = PAGE_SIZE * 100;

        std::shared_ptr<MsdArmBuffer> buffer(
            MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
        EXPECT_TRUE(buffer);

        // GPU VA not page aligned
        EXPECT_FALSE(connection->AddMapping(
            std::make_unique<GpuMapping>(1, 1, 0, connection.get(), buffer)));

        // Empty GPU VA.
        EXPECT_FALSE(connection->AddMapping(
            std::make_unique<GpuMapping>(PAGE_SIZE, 0, 0, connection.get(), buffer)));

        // size would overflow.
        EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
            1000 * PAGE_SIZE, std::numeric_limits<uint64_t>::max() - PAGE_SIZE * 100 + 1, 0,
            connection.get(), buffer)));

        // GPU VA would be larger than 48 bits wide.
        EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
            1000 * PAGE_SIZE, (1ul << 48) - 999 * PAGE_SIZE, 0, connection.get(), buffer)));

        // Map is too large for buffer.
        EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
            1000 * PAGE_SIZE, PAGE_SIZE * 101, 0, connection.get(), buffer)));

        // Invalid flags.
        EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
            1000 * PAGE_SIZE, PAGE_SIZE * 100, (1 << 14), connection.get(), buffer)));

        EXPECT_TRUE(connection->AddMapping(std::make_unique<GpuMapping>(
            1000 * PAGE_SIZE, PAGE_SIZE * 100, 0, connection.get(), buffer)));

        // Mapping would overlap previous mapping.
        EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
            1001 * PAGE_SIZE, PAGE_SIZE * 99, 0, connection.get(), buffer)));

        // Mapping would overlap next mapping.
        EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
            999 * PAGE_SIZE, PAGE_SIZE * 100, 0, connection.get(), buffer)));

        EXPECT_TRUE(connection->AddMapping(std::make_unique<GpuMapping>(
            1100 * PAGE_SIZE, PAGE_SIZE * 100, 0, connection.get(), buffer)));

        EXPECT_FALSE(connection->RemoveMapping(1001 * PAGE_SIZE));

        EXPECT_TRUE(connection->RemoveMapping(1000 * PAGE_SIZE));

        buffer.reset();

        // Mapping should already have been removed by buffer deletion.
        EXPECT_FALSE(connection->RemoveMapping(1100 * PAGE_SIZE));
    }

    void Notification()
    {
        auto connection = MsdArmConnection::Create(0, nullptr);
        EXPECT_TRUE(connection);
        MsdArmAtom atom(connection, 0, 1, 5, magma_arm_mali_user_data{7, 8});

        // Shouldn't do anything.
        connection->SendNotificationData(&atom, static_cast<ArmMaliResultCode>(10));

        connection->SetNotificationChannel(&TestSendCallback, 100);
        connection->SendNotificationData(&atom, static_cast<ArmMaliResultCode>(20));
        EXPECT_EQ(sizeof(g_status), g_test_data_size);
        EXPECT_EQ(100u, g_test_channel);

        EXPECT_EQ(7u, g_status.data.data[0]);
        EXPECT_EQ(8u, g_status.data.data[1]);
        EXPECT_EQ(20u, g_status.result_code);
        EXPECT_EQ(5u, g_status.atom_number);

        connection->SetNotificationChannel(nullptr, 0);
        connection->SendNotificationData(&atom, static_cast<ArmMaliResultCode>(20));

        EXPECT_EQ(20u, g_status.result_code);
    }
};

TEST(TestConnection, MapUnmap)
{
    TestConnection test;
    test.MapUnmap();
}

TEST(TestConnection, Notification)
{
    TestConnection test;
    test.Notification();
}
