// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <lib/ddk/platform-defs.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
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

using device_watcher::RecursiveWaitForFile;
using devmgr_integration_test::IsolatedDevmgr;

TEST(PbusTest, Enumeration) {
  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(
      {
          .sys_device_driver = "fuchsia-boot:///#driver/platform-bus.so",
      },
      &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(),
                                 "sys/platform/pt/test-board", &fd));
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
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(),
                                 "sys/platform/11:01:f/composite-dev/composite", &fd));
  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:10", &fd));
  EXPECT_OK(
      RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:12/test-spi/spi/spi-0-0", &fd));
  EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(),
                                 "sys/platform/11:01:10/composite-dev-2/composite", &fd),
            ZX_OK);
  EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:21/test-pci", &fd),
            ZX_OK);

  const int dirfd = devmgr.devfs_root().get();
  struct stat st;
  EXPECT_EQ(fstatat(dirfd, "sys/platform/pt/test-board", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-2", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-3-top", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-2/child-4", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-3-top/child-3", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:5/test-gpio/gpio-3", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:7/test-clock/clock-1", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:8/test-i2c/i2c/i2c-1-5", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:f/composite-dev/composite", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:21/test-pci", &st, 0), 0);
  EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:22/test-power-sensor", &st, 0), 0);

  // Check that we see multiple entries that begin with "fragment-" for a device that is a
  // fragment of multiple composites
  const fbl::unique_fd clock_dir(
      openat(dirfd, "sys/platform/11:01:7/test-clock/clock-1", O_DIRECTORY | O_RDONLY));
  ASSERT_TRUE(clock_dir.is_valid(), "%s", strerror(errno));
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

  fbl::unique_fd platform_bus;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &platform_bus));

  fdio_cpp::FdioCaller caller{std::move(platform_bus)};

  zx::result channel = caller.take_as<fuchsia_sysinfo::SysInfo>();
  ASSERT_OK(channel.status_value());

  const fidl::WireSyncClient<fuchsia_sysinfo::SysInfo> client(std::move(channel.value()));

  // Get board name.
  [&client]() {
    const fidl::WireResult result = client->GetBoardName();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    const std::string board_info{response.name.get()};
    EXPECT_STREQ(board_info, "driver-integration-test");
  }();

  // Get interrupt controller information.
  [&client]() {
    const fidl::WireResult result = client->GetInterruptControllerInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    ASSERT_NOT_NULL(response.info.get());
  }();

  // Get board revision information.
  [&client]() {
    const fidl::WireResult result = client->GetBoardRevision();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    ASSERT_NE(response.revision, 0);
  }();
}

}  // namespace
