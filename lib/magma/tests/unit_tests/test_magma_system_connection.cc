// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
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
    auto device = MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev));

    uint32_t device_id = device->GetDeviceId();
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
    uint32_t active_context_count_ = 0;
};

TEST(MagmaSystemConnection, ContextManagement)
{
    auto msd_connection = new MsdMockConnection_ContextManagement();

    auto msd_dev = new MsdMockDevice();
    auto dev =
        std::shared_ptr<MagmaSystemDevice>(MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev)));
    MagmaSystemConnection connection(dev, MsdConnectionUniquePtr(msd_connection),
                                     MAGMA_CAPABILITY_RENDERING);

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

TEST(MagmaSystemConnection, BufferManagement)
{
    auto msd_drv = msd_driver_create();
    ASSERT_NE(msd_drv, nullptr);
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    ASSERT_NE(msd_dev, nullptr);
    auto dev =
        std::shared_ptr<MagmaSystemDevice>(MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev)));
    auto msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    MagmaSystemConnection connection(dev, MsdConnectionUniquePtr(msd_connection),
                                     MAGMA_CAPABILITY_RENDERING);

    uint64_t test_size = 4096;

    auto buf = magma::PlatformBuffer::Create(test_size, "test");

    // assert because if this fails the rest of this is gonna be bogus anyway
    ASSERT_NE(buf, nullptr);
    EXPECT_GE(buf->size(), test_size);

    uint32_t duplicate_handle1;
    ASSERT_TRUE(buf->duplicate_handle(&duplicate_handle1));

    uint64_t id;
    EXPECT_TRUE(connection.ImportBuffer(duplicate_handle1, &id));

    // should be able to get the buffer by handle
    auto get_buf = connection.LookupBuffer(id);
    EXPECT_NE(get_buf, nullptr);
    EXPECT_EQ(get_buf->id(), id); // they are shared ptrs after all

    uint32_t duplicate_handle2;
    ASSERT_TRUE(buf->duplicate_handle(&duplicate_handle2));

    EXPECT_TRUE(connection.ImportBuffer(duplicate_handle2, &id));

    // freeing the allocated buffer should cause refcount to drop to 1
    EXPECT_TRUE(connection.ReleaseBuffer(id));
    EXPECT_NE(connection.LookupBuffer(id), nullptr);

    // freeing the allocated buffer should work
    EXPECT_TRUE(connection.ReleaseBuffer(id));

    // should no longer be able to get it from the map
    EXPECT_EQ(connection.LookupBuffer(id), nullptr);

    // should not be able to double free it
    EXPECT_FALSE(connection.ReleaseBuffer(id));
}

TEST(MagmaSystemConnection, Semaphores)
{
    auto msd_drv = msd_driver_create();
    ASSERT_NE(msd_drv, nullptr);
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    ASSERT_NE(msd_dev, nullptr);
    auto dev =
        std::shared_ptr<MagmaSystemDevice>(MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev)));
    auto msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    MagmaSystemConnection connection(dev, MsdConnectionUniquePtr(msd_connection),
                                     MAGMA_CAPABILITY_RENDERING);

    auto semaphore = magma::PlatformSemaphore::Create();

    // assert because if this fails the rest of this is gonna be bogus anyway
    ASSERT_NE(semaphore, nullptr);

    uint32_t duplicate_handle1;
    ASSERT_TRUE(semaphore->duplicate_handle(&duplicate_handle1));

    EXPECT_TRUE(connection.ImportObject(duplicate_handle1, magma::PlatformObject::SEMAPHORE));

    auto system_semaphore = connection.LookupSemaphore(semaphore->id());
    EXPECT_NE(system_semaphore, nullptr);
    EXPECT_EQ(system_semaphore->platform_semaphore()->id(), semaphore->id());

    uint32_t duplicate_handle2;
    ASSERT_TRUE(semaphore->duplicate_handle(&duplicate_handle2));

    EXPECT_TRUE(connection.ImportObject(duplicate_handle2, magma::PlatformObject::SEMAPHORE));

    // freeing the allocated semaphore should decrease refcount to 1
    EXPECT_TRUE(connection.ReleaseObject(semaphore->id(), magma::PlatformObject::SEMAPHORE));
    EXPECT_NE(connection.LookupSemaphore(semaphore->id()), nullptr);

    // freeing the allocated buffer should work
    EXPECT_TRUE(connection.ReleaseObject(semaphore->id(), magma::PlatformObject::SEMAPHORE));

    // should no longer be able to get it from the map
    EXPECT_EQ(connection.LookupSemaphore(semaphore->id()), nullptr);

    // should not be able to double free it
    EXPECT_FALSE(connection.ReleaseObject(semaphore->id(), magma::PlatformObject::SEMAPHORE));
}

TEST(MagmaSystemConnection, BufferSharing)
{
    auto msd_drv = msd_driver_create();
    ASSERT_NE(msd_drv, nullptr);
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    ASSERT_NE(msd_dev, nullptr);
    auto dev =
        std::shared_ptr<MagmaSystemDevice>(MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev)));

    auto msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    MagmaSystemConnection connection_0(dev, MsdConnectionUniquePtr(msd_connection),
                                       MAGMA_CAPABILITY_RENDERING);

    msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    MagmaSystemConnection connection_1(dev, MsdConnectionUniquePtr(msd_connection),
                                       MAGMA_CAPABILITY_RENDERING);

    auto platform_buf = magma::PlatformBuffer::Create(4096, "test");

    uint64_t buf_id_0 = 0;
    uint64_t buf_id_1 = 1;

    uint32_t duplicate_handle;
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));
    EXPECT_TRUE(connection_0.ImportBuffer(duplicate_handle, &buf_id_0));
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));
    EXPECT_TRUE(connection_1.ImportBuffer(duplicate_handle, &buf_id_1));

    // should be the same underlying memory object
    EXPECT_EQ(buf_id_0, buf_id_1);

    auto buf_0 = connection_0.LookupBuffer(buf_id_0);
    auto buf_1 = connection_1.LookupBuffer(buf_id_1);

    // should also be shared pointers to the same MagmaSystemBuffer
    EXPECT_EQ(buf_0, buf_1);
}
