// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <fuchsia/device/manager/c/fidl.h>
#include <lib/fdio/util.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace {

// Ask the kernel to run its unit tests.
bool run_kernel_unittests() {
    BEGIN_TEST;

    static const char command_string[] = "kerneldebug ut all";

    // Send the command via devmgr.
    int dmctl_fd = open("/dev/misc/dmctl", O_WRONLY);
    ASSERT_GE(dmctl_fd, 0);

    zx::channel dmctl;
    ASSERT_EQ(fdio_get_service_handle(dmctl_fd, dmctl.reset_and_get_address()), ZX_OK);

    // dmctl's ExecuteCommand() requires us to pass a socket, but we don't read
    // from the other endpoint.
    zx::socket local, remote;
    ASSERT_EQ(zx::socket::create(0, &local, &remote), ZX_OK);
    zx_status_t call_status;
    zx_status_t status = fuchsia_device_manager_ExternalControllerExecuteCommand(
            dmctl.get(), remote.release(), command_string, strlen(command_string), &call_status);

    // Check result of kernel unit tests.
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(call_status, ZX_OK);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(kernel_unittests)
RUN_TEST(run_kernel_unittests)
END_TEST_CASE(kernel_unittests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
