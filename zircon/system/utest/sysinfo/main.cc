// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <zircon/boot/image.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zxtest/zxtest.h>

#define SYSINFO_PATH    "/dev/misc/sysinfo"

TEST(SysinfoTest, GetRootResource) {
    // Get the resource handle from the driver.
    int fd = open(SYSINFO_PATH, O_RDWR);
    ASSERT_GE(fd, 0, "Can't open sysinfo");

    zx::channel channel;
    ASSERT_OK(fdio_get_service_handle(fd, channel.reset_and_get_address()),
              "Failed to get channel");

    zx_handle_t root_resource;
    zx_status_t status;
    ASSERT_OK(fuchsia_sysinfo_DeviceGetRootResource(channel.get(), &status, &root_resource),
              "Failed to get root resource");
    ASSERT_OK(status, "Failed to get root resource");

    // Make sure it's a resource with the expected rights.
    zx_info_handle_basic_t info;
    ASSERT_OK(zx_object_get_info(root_resource, ZX_INFO_HANDLE_BASIC, &info,
                                 sizeof(info), nullptr, nullptr),
              "Can't get handle info");
    EXPECT_EQ(info.type, ZX_OBJ_TYPE_RESOURCE, "Unexpected type");
    EXPECT_EQ(info.rights, ZX_RIGHT_TRANSFER, "Unexpected rights");

    // Clean up.
    EXPECT_OK(zx_handle_close(root_resource));
}

TEST(SysinfoTest, GetBoardName) {
    // Get the resource handle from the driver.
    int fd = open(SYSINFO_PATH, O_RDWR);
    ASSERT_GE(fd, 0, "Can't open sysinfo");

    zx::channel channel;
    ASSERT_OK(fdio_get_service_handle(fd, channel.reset_and_get_address()),
              "Failed to get channel");

    // Test fuchsia_sysinfo_DeviceGetBoardName().
    char board_name[ZBI_BOARD_NAME_LEN];
    zx_status_t status;
    size_t actual_size;
    zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetBoardName(channel.get(), &status, board_name,
                                                                sizeof(board_name), &actual_size);
    ASSERT_OK(fidl_status, "Failed to get board name");
    ASSERT_OK(status, "Failed to get board name");
    ASSERT_LE(actual_size, sizeof(board_name), "GetBoardName returned too much data");
    EXPECT_NE(0, board_name[0], "board name is empty");
}

TEST(SysinfoTest, GetInterruptControllerInfo) {
    // Get the resource handle from the driver.
    int fd = open(SYSINFO_PATH, O_RDWR);
    ASSERT_GE(fd, 0, "Can't open sysinfo");

    zx::channel channel;
    ASSERT_OK(fdio_get_service_handle(fd, channel.reset_and_get_address()),
              "Failed to get channel");

    // Test fuchsia_sysinfo_DeviceGetInterruptControllerInfo().
    fuchsia_sysinfo_InterruptControllerInfo info;
    zx_status_t status;
    ASSERT_OK(fuchsia_sysinfo_DeviceGetInterruptControllerInfo(channel.get(), &status, &info),
              "Failed to get interrupt controller info");
    ASSERT_OK(status, "Failed to get interrupt controller info");
    EXPECT_NE(info.type, fuchsia_sysinfo_InterruptControllerType_UNKNOWN,
              "interrupt controller type is unknown");
}
