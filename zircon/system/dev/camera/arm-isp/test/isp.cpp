// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <ddk/platform-defs.h>
#include <fbl/unique_fd.h>
#include <fuchsia/camera/test/c/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zxtest/zxtest.h>

namespace {

bool IsBoardName(const char* requested_board_name) {
    constexpr char kSysInfoPath[] = "/dev/misc/sysinfo";
    fbl::unique_fd sysinfo(open(kSysInfoPath, O_RDWR));
    if (!sysinfo) {
        return false;
    }
    zx::channel channel;
    if (fdio_get_service_handle(sysinfo.release(), channel.reset_and_get_address()) != ZX_OK) {
        return false;
    }

    char board_name[ZX_MAX_NAME_LEN];
    zx_status_t status;
    size_t actual_size;
    zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetBoardName(channel.get(), &status, board_name,
                                                                 sizeof(board_name), &actual_size);
    if (fidl_status != ZX_OK || status != ZX_OK) {
        return false;
    }
    return strcmp(board_name, requested_board_name) == 0;
}

// Integration test for the driver defined in zircon/system/dev/camera/arm-isp.
class IspTest : public zxtest::Test {
    void SetUp() override;

protected:
    fbl::unique_fd fd_;
    zx_handle_t handle_;
};

void IspTest::SetUp() {
    fbl::unique_fd devfs_root(open("/dev", O_RDWR));
    ASSERT_TRUE(devfs_root);

    zx_status_t status = devmgr_integration_test::RecursiveWaitForFile(
        devfs_root, "class/isp-device-test/000", &fd_);
    ASSERT_EQ(ZX_OK, status);

    status = fdio_get_service_handle(fd_.get(), &handle_);
    ASSERT_EQ(ZX_OK, status);
}

TEST_F(IspTest, BasicConnectionTest) {
    fuchsia_camera_test_TestReport report;
    zx_status_t out_status;
    zx_status_t status =
        fuchsia_camera_test_IspTesterRunTests(handle_, &out_status, &report);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(ZX_OK, out_status);
    EXPECT_EQ(1, report.test_count);
    EXPECT_EQ(1, report.success_count);
    EXPECT_EQ(0, report.failure_count);
}

} // namespace

int main(int argc, char** argv) {
    if (IsBoardName("sherlock")) {
        return RUN_ALL_TESTS(argc, argv);
    }

    return 0;
}
