// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <magenta/device/sysinfo.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <unittest/unittest.h>

bool get_root_resource_succeeds() {
    BEGIN_TEST;

    // Get the resource handle from the driver.
    int fd = open("/dev/misc/sysinfo", O_RDWR);
    ASSERT_GE(fd, 0, "Can't open sysinfo");

    mx_handle_t root_resource;
    ssize_t n = ioctl_sysinfo_get_root_resource(fd, &root_resource);
    close(fd);
    ASSERT_EQ(n, sizeof(root_resource), "ioctl failed");

    // Make sure it's a resource with the expected rights.
    mx_info_handle_basic_t info;
    ASSERT_EQ(mx_object_get_info(root_resource, MX_INFO_HANDLE_BASIC, &info,
                                 sizeof(info), nullptr, nullptr),
              MX_OK, "Can't get handle info");
    EXPECT_EQ(info.type, MX_OBJ_TYPE_RESOURCE, "Unexpected type");
    EXPECT_EQ(info.rights, MX_RIGHT_TRANSFER, "Unexpected rights");

    // Clean up.
    EXPECT_EQ(mx_handle_close(root_resource), MX_OK);

    END_TEST;
}

BEGIN_TEST_CASE(sysinfo_tests)
RUN_TEST(get_root_resource_succeeds)
END_TEST_CASE(sysinfo_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
