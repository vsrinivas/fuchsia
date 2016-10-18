// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system.h"
#include "magma_system_display_abi.h"
#include "mock/mock_msd.h"
#include "sys_driver/magma_system_connection.h"
#include "sys_driver/magma_system_device.h"
#include "gtest/gtest.h"

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

TEST(MagmaSystemDisplay, PageFlip)
{
    auto msd_drv = msd_driver_create();
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    auto dev = MagmaSystemDevice(MsdDeviceUniquePtr(msd_dev));
    uint32_t connection_handle;
    ASSERT_TRUE(dev.Open(0, &connection_handle));
    auto connection = magma::PlatformIpcConnection::Create(connection_handle);
    ASSERT_NE(connection, nullptr);
    auto display = dev.OpenDisplay();
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

    uint32_t token;
    uint32_t imported_handle;
    magma_system_export(connection.get(), buf_handle, &token);
    EXPECT_EQ(magma_system_get_error(connection.get()), 0);
    magma_system_display_import_buffer(display.get(), token, &imported_handle);
    EXPECT_EQ(magma_system_display_get_error(display.get()), 0);

    // should be ok to page flip now
    auto test_success = std::unique_ptr<TestPageFlip>(new TestPageFlip(0));
    magma_system_display_page_flip(display.get(), imported_handle, &callback, test_success.get());

    msd_driver_destroy(msd_drv);
}