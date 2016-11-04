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
    auto connection = MagmaSystemConnection(&dev, MsdConnectionUniquePtr(msd_connection),
                                            MAGMA_SYSTEM_CAPABILITY_RENDERING);

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
    auto dev = MagmaSystemDevice(MsdDeviceUniquePtr(msd_dev));
    auto msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    auto connection = MagmaSystemConnection(&dev, MsdConnectionUniquePtr(msd_connection),
                                            MAGMA_SYSTEM_CAPABILITY_RENDERING);

    uint64_t test_size = 4096;

    auto buf = magma::PlatformBuffer::Create(test_size);

    // assert because if this fails the rest of this is gonna be bogus anyway
    ASSERT_NE(buf, nullptr);
    EXPECT_GE(buf->size(), test_size);

    uint32_t duplicate_handle;
    ASSERT_TRUE(buf->duplicate_handle(&duplicate_handle));

    uint64_t id;
    connection.ImportBuffer(duplicate_handle, &id);

    // should be able to get the buffer by handle
    auto get_buf = connection.LookupBuffer(id);
    EXPECT_NE(get_buf, nullptr);
    EXPECT_EQ(get_buf->id(), id); // they are shared ptrs after all

    // freeing the allocated buffer should work
    EXPECT_TRUE(connection.ReleaseBuffer(id));

    // should no longer be able to get it from the map
    EXPECT_EQ(connection.LookupBuffer(id), nullptr);

    // should not be able to double free it
    EXPECT_FALSE(connection.ReleaseBuffer(id));
}

TEST(MagmaSystemConnection, BufferSharing)
{
    auto msd_drv = msd_driver_create();
    ASSERT_NE(msd_drv, nullptr);
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    ASSERT_NE(msd_dev, nullptr);
    auto dev = MagmaSystemDevice(MsdDeviceUniquePtr(msd_dev));

    auto msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    auto connection_0 = MagmaSystemConnection(&dev, MsdConnectionUniquePtr(msd_connection),
                                              MAGMA_SYSTEM_CAPABILITY_RENDERING);

    msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    auto connection_1 = MagmaSystemConnection(&dev, MsdConnectionUniquePtr(msd_connection),
                                              MAGMA_SYSTEM_CAPABILITY_RENDERING);

    auto platform_buf = magma::PlatformBuffer::Create(4096);

    uint64_t buf_id_0 = 0;
    uint64_t buf_id_1 = 1;

    uint32_t duplicate_handle;
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));
    connection_0.ImportBuffer(duplicate_handle, &buf_id_0);
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));
    connection_1.ImportBuffer(duplicate_handle, &buf_id_1);

    // should be the same underlying memory object
    EXPECT_EQ(buf_id_0, buf_id_1);

    auto buf_0 = connection_0.LookupBuffer(buf_id_0);
    auto buf_1 = connection_1.LookupBuffer(buf_id_1);

    // should also be shared pointers to the same MagmaSystemBuffer
    EXPECT_EQ(buf_0, buf_1);
}

class TestPageFlip {
public:
    TestPageFlip(int32_t expected_error) : expected_error_(expected_error) {}

    void Test(int32_t error) { EXPECT_EQ(error, expected_error_); }
private:
    int32_t expected_error_;
};

void callback(int32_t error, void* data)
{
    EXPECT_NE(data, nullptr);
    reinterpret_cast<TestPageFlip*>(data)->Test(error);
}

TEST(MagmaSystemConnection, PageFlip)
{
    auto msd_drv = msd_driver_create();
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    auto dev = MagmaSystemDevice(MsdDeviceUniquePtr(msd_dev));

    uint32_t connection_handle;

    ASSERT_TRUE(dev.Open(0, MAGMA_SYSTEM_CAPABILITY_RENDERING, &connection_handle));
    auto connection = magma::PlatformIpcConnection::Create(connection_handle);
    ASSERT_NE(connection, nullptr);

    ASSERT_TRUE(dev.Open(0, MAGMA_SYSTEM_CAPABILITY_DISPLAY, &connection_handle));
    auto display = magma::PlatformIpcConnection::Create(connection_handle);
    ASSERT_NE(display, nullptr);

    // should be unable to pageflip totally bogus handle
    auto test_invalid = std::unique_ptr<TestPageFlip>(new TestPageFlip(-EINVAL));
    magma_system_display_page_flip(display.get(), 0, &callback, test_invalid.get());

    uint64_t buf_size;
    uint32_t buf_handle;
    magma_system_alloc(connection.get(), PAGE_SIZE, &buf_size, &buf_handle);
    ASSERT_EQ(magma_system_get_error(connection.get()), 0);

    // should still be unable to page flip buffer because it hasnt been exported to display
    magma_system_display_page_flip(display.get(), buf_handle, &callback, test_invalid.get());

    uint32_t buffer_handle;
    uint32_t imported_handle;
    magma_system_export(connection.get(), buf_handle, &buffer_handle);
    EXPECT_EQ(magma_system_get_error(connection.get()), 0);
    magma_system_import(display.get(), buffer_handle, &imported_handle);
    EXPECT_EQ(magma_system_get_error(display.get()), 0);

    // should be ok to page flip now
    auto test_success = std::unique_ptr<TestPageFlip>(new TestPageFlip(0));
    magma_system_display_page_flip(display.get(), imported_handle, &callback, test_success.get());

    msd_driver_destroy(msd_drv);
}
