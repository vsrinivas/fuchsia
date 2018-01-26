// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <unittest/unittest.h>
#include <zircon/device/dmctl.h>
#include <zircon/syscalls.h>

// Ask the kernel to run its unit tests.
bool run_kernel_unittests() {
    BEGIN_TEST;

    static const char command_string[] = "kerneldebug ut all";

    // Send the command via devmgr.
    int dmctl_fd = open("/dev/misc/dmctl", O_WRONLY);
    ASSERT_GE(dmctl_fd, 0);
    dmctl_cmd_t cmd;
    ASSERT_LE(sizeof(command_string), sizeof(cmd.name));
    strcpy(cmd.name, command_string);
    // devmgr's ioctl() requires us to pass a socket, but we don't read
    // from the other endpoint.
    zx_handle_t handle;
    ASSERT_EQ(zx_socket_create(0, &cmd.h, &handle), ZX_OK);
    ssize_t result = ioctl_dmctl_command(dmctl_fd, &cmd);
    ASSERT_EQ(close(dmctl_fd), 0);
    ASSERT_EQ(zx_handle_close(handle), ZX_OK);

    // Check result of kernel unit tests.
    ASSERT_EQ(result, ZX_OK);

    END_TEST;
}

BEGIN_TEST_CASE(kernel_unittests)
RUN_TEST(run_kernel_unittests)
END_TEST_CASE(kernel_unittests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
