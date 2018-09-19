// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <zircon/boot/image.h>
#include <zircon/device/sysinfo.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <unittest/unittest.h>

#define SYSINFO_PATH    "/dev/misc/sysinfo"

bool get_root_resource_succeeds() {
    BEGIN_TEST;

    // Get the resource handle from the driver.
    int fd = open(SYSINFO_PATH, O_RDWR);
    ASSERT_GE(fd, 0, "Can't open sysinfo");

    zx_handle_t root_resource;
    ssize_t n = ioctl_sysinfo_get_root_resource(fd, &root_resource);
    close(fd);
    ASSERT_EQ(n, sizeof(root_resource), "ioctl failed");

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

    // Test ioctl_sysinfo_get_board_name().
    char board_name[ZBI_BOARD_NAME_LEN];
    ssize_t n = ioctl_sysinfo_get_board_name(fd, board_name, sizeof(board_name));
    ASSERT_GT(n, 0, "ioctl_sysinfo_get_board_name failed");
    ASSERT_LE((size_t)n, sizeof(board_name), "ioctl_sysinfo_get_board_name returned too much data");
    ASSERT_NE(0, board_name[0], "board name is empty");
    ASSERT_EQ(0, board_name[n - 1], "board name is not zero terminated");

    close(fd);

    END_TEST;
}

bool get_interrupt_controller_info_succeeds() {
    BEGIN_TEST;

    // Get the resource handle from the driver.
    int fd = open(SYSINFO_PATH, O_RDWR);
    ASSERT_GE(fd, 0, "Can't open sysinfo");

    // Test ioctl_sysinfo_get_board_name().
    interrupt_controller_info_t info;
    ssize_t n = ioctl_sysinfo_get_interrupt_controller_info(fd, &info);
    ASSERT_EQ(n, sizeof(info), "ioctl_sysinfo_get_interrupt_controller_info failed");
    EXPECT_NE(info.type, INTERRUPT_CONTROLLER_TYPE_UNKNOWN, "interrupt controller type is unknown");

    close(fd);

    END_TEST;
}

BEGIN_TEST_CASE(sysinfo_tests)
RUN_TEST(get_root_resource_succeeds)
RUN_TEST(get_board_name_succeeds)
RUN_TEST(get_interrupt_controller_info_succeeds)
END_TEST_CASE(sysinfo_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
