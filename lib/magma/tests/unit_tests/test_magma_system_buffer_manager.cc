// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sys_driver/magma_system_buffer_manager.h"
#include "sys_driver/magma_system_device.h"
#include "gtest/gtest.h"

TEST(MagmaSystemBufferManager, BufferManagement)
{
    auto msd_drv = msd_driver_create();
    ASSERT_NE(msd_drv, nullptr);
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    ASSERT_NE(msd_dev, nullptr);
    auto dev = MagmaSystemDevice(MsdDeviceUniquePtr(msd_dev));
    auto bufmgr = MagmaSystemBufferManager(&dev);

    uint64_t test_size = 4096;

    auto buf = magma::PlatformBuffer::Create(test_size);

    // assert because if this fails the rest of this is gonna be bogus anyway
    ASSERT_NE(buf, nullptr);
    EXPECT_GE(buf->size(), test_size);

    uint32_t duplicate_handle;
    ASSERT_TRUE(buf->duplicate_handle(&duplicate_handle));

    uint64_t id;
    bufmgr.ImportBuffer(duplicate_handle, &id);

    // should be able to get the buffer by handle
    auto get_buf = bufmgr.LookupBuffer(id);
    EXPECT_NE(get_buf, nullptr);
    EXPECT_EQ(get_buf->id(), id); // they are shared ptrs after all

    // freeing the allocated buffer should work
    EXPECT_TRUE(bufmgr.ReleaseBuffer(id));

    // should no longer be able to get it from the map
    EXPECT_EQ(bufmgr.LookupBuffer(id), nullptr);

    // should not be able to double free it
    EXPECT_FALSE(bufmgr.ReleaseBuffer(id));
}

TEST(MagmaSystemConnection, BufferSharing)
{
    auto msd_drv = msd_driver_create();
    ASSERT_NE(msd_drv, nullptr);
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    ASSERT_NE(msd_dev, nullptr);
    auto dev = MagmaSystemDevice(MsdDeviceUniquePtr(msd_dev));

    auto bufmgr_0 = MagmaSystemBufferManager(&dev);
    auto bufmgr_1 = MagmaSystemBufferManager(&dev);

    auto platform_buf = magma::PlatformBuffer::Create(4096);

    uint64_t buf_id_0 = 0;
    uint64_t buf_id_1 = 1;

    uint32_t duplicate_handle;
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));
    bufmgr_0.ImportBuffer(duplicate_handle, &buf_id_0);
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));
    bufmgr_1.ImportBuffer(duplicate_handle, &buf_id_1);

    // should be the same underlying memory object
    EXPECT_EQ(buf_id_0, buf_id_1);

    auto buf_0 = bufmgr_0.LookupBuffer(buf_id_0);
    auto buf_1 = bufmgr_1.LookupBuffer(buf_id_1);

    // should also be shared pointers to the same MagmaSystemBuffer
    EXPECT_EQ(buf_0, buf_1);
}