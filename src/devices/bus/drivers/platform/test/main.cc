// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/ddk/platform-defs.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
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

TEST(PbusTest, Enumeration) {
  devmgr_launcher::Args args;
  args.sys_device_driver = "/boot/driver/platform-bus.so";
  args.driver_search_paths.push_back("/boot/driver");
  args.get_boot_item = GetBootItem;

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/test-board", &fd));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:1", &fd));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:1/child-1", &fd));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:1/child-1/child-2", &fd));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(),
                                 "sys/platform/11:01:1/child-1/child-2/child-4", &fd));
  EXPECT_OK(
      RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:1/child-1/child-3-top", &fd));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(),
                                 "sys/platform/11:01:1/child-1/child-3-top/child-3", &fd));
  EXPECT_OK(
      RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:5/test-gpio/gpio-3", &fd));
  EXPECT_OK(
      RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:7/test-clock/clock-1", &fd));
  EXPECT_OK(
      RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:8/test-i2c/i2c/i2c-1-5", &fd));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:f", &fd));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "composite-dev/composite", &fd));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:10", &fd));
  EXPECT_OK(
      RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:12/test-spi/spi/spi-0-0", &fd));
  EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "composite-dev-2/composite", &fd), ZX_OK);
  EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:19/test-rpmb", &fd),
            ZX_OK);
  EXPECT_EQ(
      RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:1c/test-goldfish-pipe", &fd),
      ZX_OK);
  EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(),
                                 "sys/platform/11:01:1d/test-goldfish-address-space", &fd),
            ZX_OK);
  EXPECT_EQ(
      RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:20/test-goldfish-sync", &fd),
      ZX_OK);

  const int dirfd = devmgr.devfs_root().get();
  struct stat st;
  EXPECT_EQ(fstatat(dirfd, "sys/platform/test-board", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-2", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-3-top", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-2/child-4", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-3-top/child-3", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:5/test-gpio/gpio-3", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:7/test-clock/clock-1", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:8/test-i2c/i2c/i2c-1-5", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "composite-dev/composite", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:19/test-rpmb", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1c/test-goldfish-pipe", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1d/test-goldfish-address-space", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:20/test-goldfish-sync", &st, 0), 0);

  // Check that we see multiple entries that begin with "fragment-" for a device that is a
  // fragment of multiple composites
  fbl::unique_fd clock_dir(
      openat(dirfd, "sys/platform/11:01:7/test-clock/clock-1", O_DIRECTORY | O_RDONLY));
  size_t devices_seen = 0;
  ASSERT_EQ(
      fdio_watch_directory(
          clock_dir.get(),
          [](int dirfd, int event, const char* fn, void* cookie) {
            auto devices_seen = static_cast<size_t*>(cookie);
            if (event == WATCH_EVENT_ADD_FILE && !strncmp(fn, "fragment-", strlen("fragment-"))) {
              *devices_seen += 1;
            }
            if (event == WATCH_EVENT_WAITING) {
              return ZX_ERR_STOP;
            }
            return ZX_OK;
          },
          ZX_TIME_INFINITE, &devices_seen),
      ZX_ERR_STOP);
  ASSERT_EQ(devices_seen, 2);
}

TEST(PbusTest, BoardInfo) {
  devmgr_launcher::Args args;
  args.sys_device_driver = "/boot/driver/platform-bus.so";
  args.driver_search_paths.push_back("/boot/driver");
  args.get_boot_item = GetBootItem;

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd platform_bus;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &platform_bus));

  zx::channel channel;
  ASSERT_OK(fdio_get_service_handle(platform_bus.release(), channel.reset_and_get_address()));

  fuchsia_sysinfo::SysInfo::SyncClient client(std::move(channel));
  // Get board name.
  auto board_info = client.GetBoardName();
  EXPECT_OK(board_info.status());
  EXPECT_TRUE(board_info.ok());
  EXPECT_BYTES_EQ(board_info->name.cbegin(), "pbus-test", board_info->name.size());
  EXPECT_EQ(board_info->name.size(), strlen("pbus-test"));

  // Get interrupt controller information.
  auto irq_ctrl_info = client.GetInterruptControllerInfo();
  EXPECT_OK(irq_ctrl_info.status());
  EXPECT_TRUE(irq_ctrl_info.ok());
  EXPECT_NE(nullptr, irq_ctrl_info->info);

  // Get board revision information.
  auto board_revision = client.GetBoardRevision();
  EXPECT_OK(board_revision.status());
  EXPECT_TRUE(board_revision.ok());
}

}  // namespace
