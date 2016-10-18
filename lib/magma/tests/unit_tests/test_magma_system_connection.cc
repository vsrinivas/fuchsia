// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system.h"
#include "mock/mock_msd.h"
#include "sys_driver/magma_system_connection.h"
#include "sys_driver/magma_system_device.h"
#include "gtest/gtest.h"

class MsdMockDevice_GetDeviceId : public MsdMockDevice {
public:
    MsdMockDevice_GetDeviceId(uint32_t device_id) : device_id_(device_id) {}

    uint32_t GetDeviceId() override { return device_id_; }

private:
    uint32_t device_id_;
};

TEST(MagmaSystemConnection, GetDeviceId)
{
    uint32_t test_id = 0xdeadbeef;

    auto msd_dev = new MsdMockDevice_GetDeviceId(test_id);
    auto dev = MagmaSystemDevice(MsdDeviceUniquePtr(msd_dev));

    uint32_t device_id = dev.GetDeviceId();
    // For now device_id is invalid
    EXPECT_EQ(device_id, test_id);
}


class MsdMockConnection_ContextManagement : public MsdMockConnection {
public:
    MsdMockConnection_ContextManagement() {}

    MsdMockContext* CreateContext() override
    {
        active_context_count_++;
        return MsdMockConnection::CreateContext();
    }

    void DestroyContext(MsdMockContext* ctx) override
    {
        active_context_count_--;
        MsdMockConnection::DestroyContext(ctx);
    }

    uint32_t NumActiveContexts() { return active_context_count_; }

private:
    uint32_t active_context_count_;
};

TEST(MagmaSystemConnection, ContextManagement)
{
    auto msd_connection = new MsdMockConnection_ContextManagement();

    auto msd_dev = new MsdMockDevice();
    auto dev = MagmaSystemDevice(MsdDeviceUniquePtr(msd_dev));
    auto connection = MagmaSystemConnection(&dev, MsdConnectionUniquePtr(msd_connection));

    EXPECT_EQ(msd_connection->NumActiveContexts(), 0u);

    uint32_t context_id_0 = 0;
    uint32_t context_id_1 = 1;

    EXPECT_TRUE(connection.CreateContext(context_id_0));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 1u);

    EXPECT_TRUE(connection.CreateContext(context_id_1));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 2u);

    EXPECT_TRUE(connection.DestroyContext(context_id_0));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 1u);
    EXPECT_FALSE(connection.DestroyContext(context_id_0));

    EXPECT_TRUE(connection.DestroyContext(context_id_1));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 0u);
    EXPECT_FALSE(connection.DestroyContext(context_id_1));
}
