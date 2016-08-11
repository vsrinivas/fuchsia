// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mock/mock_msd.h"
#include "sys_driver/magma_system.h"
#include "sys_driver/magma_system_connection.h"
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
    auto dev = MagmaSystemDevice(msd_device_unique_ptr_t(msd_dev, &msd_driver_destroy_device));

    uint32_t device_id = dev.GetDeviceId();
    // For now device_id is invalid
    EXPECT_EQ(device_id, test_id);
}

TEST(MagmaSystemConnection, BufferManagement)
{
    auto msd_drv = msd_driver_create();
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    auto dev = MagmaSystemDevice(msd_device_unique_ptr_t(msd_dev, &msd_driver_destroy_device));
    auto connection = dev.Open(0);
    ASSERT_NE(connection, nullptr);

    uint64_t test_size = 4096;

    {
        // allocating a zero size buffer should fail
        EXPECT_EQ(connection->AllocateBuffer(0), nullptr);

        auto buf = connection->AllocateBuffer(test_size);
        // assert because if this fails the rest of this is gonna be bogus anyway
        ASSERT_NE(buf, nullptr);
        EXPECT_GE(buf->size(), test_size);

        auto handle = buf->handle();
        EXPECT_EQ(handle, buf->platform_buffer()->handle());

        // should be able to get the buffer by handle
        auto get_buf = connection->LookupBuffer(handle);
        EXPECT_NE(get_buf, nullptr);
        EXPECT_EQ(get_buf, buf); // they are shared ptrs after all

        // freeing the allocated buffer should work
        EXPECT_TRUE(connection->FreeBuffer(handle));

        // should no longer be able to get it from the map
        EXPECT_EQ(connection->LookupBuffer(handle), nullptr);

        // should not be able to double free it
        EXPECT_FALSE(connection->FreeBuffer(handle));
    }

    msd_driver_destroy(msd_drv);
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
    auto dev = MagmaSystemDevice(msd_device_unique_ptr_t(msd_dev, &msd_driver_destroy_device));
    auto connection = MagmaSystemConnection(
        &dev, msd_connection_unique_ptr_t(msd_connection, &msd_connection_close));

    EXPECT_EQ(msd_connection->NumActiveContexts(), 0u);

    uint32_t context_id_0;
    uint32_t context_id_1;

    EXPECT_TRUE(connection.CreateContext(&context_id_0));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 1u);

    EXPECT_TRUE(magma_system_create_context(&connection, &context_id_1));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 2u);

    EXPECT_NE(context_id_0, context_id_1);

    EXPECT_TRUE(connection.DestroyContext(context_id_0));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 1u);
    EXPECT_FALSE(connection.DestroyContext(context_id_0));

    EXPECT_TRUE(magma_system_destroy_context(&connection, context_id_1));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 0u);
    EXPECT_FALSE(magma_system_destroy_context(&connection, context_id_1));
}
