// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <button_checker.h>
#include <fcntl.h>
#include <fuchsia/camera/test/c/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ddk/platform-defs.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

constexpr uint32_t kWrongBoard = 1;

zx_status_t IsBoardName(const char* requested_board_name) {
  constexpr char kSysInfoPath[] = "/svc/fuchsia.sysinfo.SysInfo";
  fbl::unique_fd sysinfo(open(kSysInfoPath, O_RDWR));
  if (!sysinfo) {
    printf("Failed to open sysinfo\n");
    return ZX_ERR_IO;
  }
  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(sysinfo.release(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    printf("Failed to fdio_get_service_handle of sysinfo, status = %d\n", status);
    return status;
  }

  char board_name[ZX_MAX_NAME_LEN];
  size_t actual_size;
  zx_status_t fidl_status = fuchsia_sysinfo_SysInfoGetBoardName(channel.get(), &status, board_name,
                                                               sizeof(board_name), &actual_size);
  if (status != ZX_OK) {
    printf("Failed to fuchsia_sysinfo_SysInfoGetBoardName. status = %d\n", status);
    return status;
  }
  if (fidl_status != ZX_OK) {
    printf("Failed to send fidl message. status = %d\n", fidl_status);
    return fidl_status;
  }
  board_name[actual_size] = '\0';
  if (actual_size != strlen(requested_board_name) ||
      strncmp(board_name, requested_board_name, actual_size) != 0) {
    printf("Wrong Board.  Expected %s, board name: %s.\n", requested_board_name, board_name);
    return kWrongBoard;
  }
  return ZX_OK;
}

// Integration test for the driver defined in zircon/system/dev/camera/arm-isp.
class IspTest : public zxtest::Test {
 public:
  void SetUp() override;

  fbl::unique_fd fd_;
  zx_handle_t handle_;
};

void IspTest::SetUp() {
  // We have to open the directory directly, since RecursiveWaitForFile
  // does allow waiting on folders from devmgr. (ZX-3249)
  fbl::unique_fd devfs_root(open("/dev/class/isp-device-test", O_RDONLY));
  ASSERT_TRUE(devfs_root);

  zx_status_t status = devmgr_integration_test::RecursiveWaitForFile(devfs_root, "000", &fd_);
  ASSERT_OK(status);

  status = fdio_get_service_handle(fd_.get(), &handle_);
  ASSERT_OK(status);
}

TEST_F(IspTest, BasicConnectionTest) {
  fuchsia_camera_test_TestReport report;
  zx_status_t out_status;
  zx_status_t status = fuchsia_camera_test_IspTesterRunTests(handle_, &out_status, &report);
  ASSERT_OK(status);
  ASSERT_OK(out_status);
  EXPECT_EQ(report.success_count, report.test_count);
  EXPECT_EQ(0, report.failure_count);
}

}  // namespace

int main(int argc, char** argv) {
  zx_status_t status = IsBoardName("sherlock");
  if (status == ZX_OK) {
    printf("Sherlock detected, running tests.\n");
    if (!VerifyDeviceUnmuted()) {
      return 0;
    }
    return RUN_ALL_TESTS(argc, argv);
  }
  if (status == kWrongBoard) {
    return 0;
  }
  return status;
}
