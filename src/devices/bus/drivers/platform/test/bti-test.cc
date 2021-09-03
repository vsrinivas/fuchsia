// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.btitest/cpp/wire.h>
#include <lib/ddk/platform-defs.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/time.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

#include <zxtest/zxtest.h>

namespace {

using devmgr_integration_test::IsolatedDevmgr;
using devmgr_integration_test::RecursiveWaitForFile;

zbi_platform_id_t kPlatformId = []() {
  zbi_platform_id_t plat_id = {};
  plat_id.vid = PDEV_VID_TEST;
  plat_id.pid = PDEV_PID_PBUS_TEST;
  strcpy(plat_id.board_name, "pbus-test");
  return plat_id;
}();

#define BOARD_REVISION_TEST 42

const zbi_board_info_t kBoardInfo = []() {
  zbi_board_info_t board_info = {};
  board_info.revision = BOARD_REVISION_TEST;
  return board_info;
}();

zx_status_t GetBootItem(uint32_t type, uint32_t extra, zx::vmo* out, uint32_t* length) {
  zx::vmo vmo;
  switch (type) {
    case ZBI_TYPE_PLATFORM_ID: {
      zx_status_t status = zx::vmo::create(sizeof(kPlatformId), 0, &vmo);
      if (status != ZX_OK) {
        return status;
      }
      status = vmo.write(&kPlatformId, 0, sizeof(kPlatformId));
      if (status != ZX_OK) {
        return status;
      }
      break;
    }
    case ZBI_TYPE_DRV_BOARD_INFO: {
      zbi_board_info_t board_info = kBoardInfo;
      zx_status_t status = zx::vmo::create(sizeof(kBoardInfo), 0, &vmo);
      if (status != ZX_OK) {
        return status;
      }
      status = vmo.write(&board_info, 0, sizeof(board_info));
      if (status != ZX_OK) {
        return status;
      }
      *length = sizeof(board_info);
      break;
    }
    default:
      break;
  }

  *out = std::move(vmo);
  return ZX_OK;
}

constexpr char kParentPath[] = "sys/platform/11:01:1a";
constexpr char kDevicePath[] = "sys/platform/11:01:1a/test-bti";

TEST(PbusBtiTest, BtiIsSameAfterCrash) {
  devmgr_launcher::Args args;
  args.sys_device_driver = "/boot/driver/platform-bus.so";
  args.get_boot_item = GetBootItem;

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), kDevicePath, &fd));
  zx::channel chan;
  ASSERT_OK(fdio_get_service_handle(fd.release(), chan.reset_and_get_address()));

  fidl::WireSyncClient<fuchsia_hardware_btitest::BtiDevice> client(std::move(chan));
  uint64_t koid1;
  {
    auto result = client.GetKoid();
    ASSERT_OK(result.status());
    koid1 = result->koid;
  }

  fd.reset(openat(devmgr.devfs_root().get(), kParentPath, O_DIRECTORY | O_RDONLY));
  std::unique_ptr<devmgr_integration_test::DirWatcher> watcher;
  ASSERT_OK(devmgr_integration_test::DirWatcher::Create(std::move(fd), &watcher));

  {
    auto result = client.Crash();
    ASSERT_OK(result.status());
  }

  // We implicitly rely on driver host being rebound in the event of a crash.
  ASSERT_OK(watcher->WaitForRemoval("test-bti", zx::duration::infinite()));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), kDevicePath, &fd));
  ASSERT_OK(fdio_get_service_handle(fd.release(), chan.reset_and_get_address()));
  *client.mutable_channel() = std::move(chan);

  uint64_t koid2;
  {
    auto result = client.GetKoid();
    ASSERT_OK(result.status());
    koid2 = result->koid;
  }
  ASSERT_EQ(koid1, koid2);
}

}  // namespace
