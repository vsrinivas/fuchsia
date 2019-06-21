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
#include <unittest/unittest.h>

#define SYSINFO_PATH    "/dev/misc/sysinfo"

bool get_root_resource_succeeds() {
    BEGIN_TEST;

    // Get the resource handle from the driver.
    int fd = open(SYSINFO_PATH, O_RDWR);
    ASSERT_GE(fd, 0, "Can't open sysinfo");

    zx::channel channel;
    ASSERT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK,
              "Failed to get channel");

    zx_handle_t root_resource;
    zx_status_t status;
    ASSERT_EQ(fuchsia_sysinfo_DeviceGetRootResource(channel.get(), &status, &root_resource), ZX_OK,
              "Failed to get root resource");
    ASSERT_EQ(status, ZX_OK, "Failed to get root resource");

    // Make sure it's a resource with the expected rights.
    zx_info_handle_basic_t info;
    ASSERT_EQ(zx_object_get_info(root_resource, ZX_INFO_HANDLE_BASIC, &info,
                                 sizeof(info), nullptr, nullptr),
              ZX_OK, "Can't get handle info");
    EXPECT_EQ(info.type, ZX_OBJ_TYPE_RESOURCE, "Unexpected type");
    EXPECT_EQ(info.rights, ZX_RIGHT_TRANSFER, "Unexpected rights");

    // Clean up.
    EXPECT_EQ(zx_handle_close(root_resource), ZX_OK);

    END_TEST;
}

bool get_board_name_succeeds() {
    BEGIN_TEST;

    // Get the resource handle from the driver.
    int fd = open(SYSINFO_PATH, O_RDWR);
    ASSERT_GE(fd, 0, "Can't open sysinfo");

    zx::channel channel;
    ASSERT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK,
              "Failed to get channel");

    // Test fuchsia_sysinfo_DeviceGetBoardName().
    char board_name[ZBI_BOARD_NAME_LEN];
    zx_status_t status;
    size_t actual_size;
    zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetBoardName(channel.get(), &status, board_name,
                                                                sizeof(board_name), &actual_size);
    ASSERT_EQ(fidl_status, ZX_OK, "Failed to get board name");
    ASSERT_EQ(status, ZX_OK, "Failed to get board name");
    ASSERT_LE(actual_size, sizeof(board_name), "GetBoardName returned too much data");
    EXPECT_NE(0, board_name[0], "board name is empty");

    END_TEST;
}

bool get_interrupt_controller_info_succeeds() {
    BEGIN_TEST;

    // Get the resource handle from the driver.
    int fd = open(SYSINFO_PATH, O_RDWR);
    ASSERT_GE(fd, 0, "Can't open sysinfo");

    zx::channel channel;
    ASSERT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()), ZX_OK,
              "Failed to get channel");

    // Test fuchsia_sysinfo_DeviceGetInterruptControllerInfo().
    fuchsia_sysinfo_InterruptControllerInfo info;
    zx_status_t status;
    ASSERT_EQ(fuchsia_sysinfo_DeviceGetInterruptControllerInfo(channel.get(), &status, &info),
              ZX_OK, "Failed to get interrupt controller info");
    ASSERT_EQ(status, ZX_OK, "Failed to get interrupt controller info");
    EXPECT_NE(info.type, fuchsia_sysinfo_InterruptControllerType_UNKNOWN,
              "interrupt controller type is unknown");

    END_TEST;
}

BEGIN_TEST_CASE(sysinfo_tests)
RUN_TEST(get_root_resource_succeeds)
RUN_TEST(get_board_name_succeeds)
RUN_TEST(get_interrupt_controller_info_succeeds)
END_TEST_CASE(sysinfo_tests)
