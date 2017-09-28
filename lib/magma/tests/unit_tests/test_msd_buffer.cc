// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "msd.h"
#include "platform_buffer.h"
#include "gtest/gtest.h"

TEST(MsdBuffer, ImportAndDestroy)
{
    auto platform_buf = magma::PlatformBuffer::Create(4096, "test");
    ASSERT_NE(platform_buf, nullptr);

    uint32_t duplicate_handle;
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));

    auto msd_buffer = msd_buffer_import(duplicate_handle);
    ASSERT_NE(msd_buffer, nullptr);

    msd_buffer_destroy(msd_buffer);
}

TEST(MsdBuffer, MapAndUnmap)
{
    msd_driver_t* driver = msd_driver_create();
    ASSERT_NE(driver, nullptr);

    msd_device_t* device = msd_driver_create_device(driver, GetTestDeviceHandle());
    EXPECT_NE(device, nullptr);

    constexpr uint64_t kBufferSize = 4096;
    auto platform_buf = magma::PlatformBuffer::Create(kBufferSize, "test");
    ASSERT_NE(platform_buf, nullptr);

    uint32_t duplicate_handle;
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));

    auto msd_buffer = msd_buffer_import(duplicate_handle);
    ASSERT_NE(msd_buffer, nullptr);

    auto connection = msd_device_open(device, 0);

    msd_connection_map_buffer_gpu(connection, msd_buffer, 0,
                                  MAGMA_GPU_MAP_FLAG_READ | MAGMA_GPU_MAP_FLAG_WRITE);

    // Ensure it can map twice.
    msd_connection_map_buffer_gpu(connection, msd_buffer, PAGE_SIZE * 1024,
                                  MAGMA_GPU_MAP_FLAG_READ | MAGMA_GPU_MAP_FLAG_EXECUTE);

    // Try to unmap a region that doesn't exist.
    msd_connection_unmap_buffer_gpu(connection, msd_buffer, PAGE_SIZE * 2048);

    // Commit an invalid region.
    msd_connection_commit_buffer(connection, msd_buffer, 1, 2);

    // Commit the entire valid region.
    msd_connection_commit_buffer(connection, msd_buffer, 0, 1);

    msd_connection_unmap_buffer_gpu(connection, msd_buffer, 0);

    // One mapping is still outstanding, and this should release it.
    msd_connection_close(connection);

    msd_buffer_destroy(msd_buffer);
    msd_device_destroy(device);

    msd_driver_destroy(driver);
}
