// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ddk/platform-defs.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fzl/fdio.h>
#include <zircon/status.h>
#include <zxtest/zxtest.h>

namespace {

using devmgr_integration_test::RecursiveWaitForFile;
using driver_integration_test::IsolatedDevmgr;

const board_test::DeviceEntry kDeviceEntry = []() {
  board_test::DeviceEntry entry = {};
  strcpy(entry.name, "fallback-rtc");
  entry.vid = PDEV_VID_GENERIC;
  entry.pid = PDEV_PID_GENERIC;
  entry.did = PDEV_DID_RTC_FALLBACK;
  return entry;
}();

TEST(DriverIntegrationTest, EnumerationTest) {
  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.device_list.push_back(kDeviceEntry);

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/test-board", &fd));

  EXPECT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/00:00:f/fallback-rtc", &fd));
}

TEST(DriverIntegrationTest, BoardName) {
  constexpr char kBoardName[] = "Random Board";

  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.board_name = kBoardName;

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "misc/sysinfo", &fd));

  fzl::FdioCaller caller(std::move(fd));
  auto result = ::llcpp::fuchsia::sysinfo::Device::Call::GetBoardName(caller.channel());
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_BYTES_EQ(result->name.data(), kBoardName, result->name.size());
}

TEST(DriverIntegrationTest, BoardRevision) {
  constexpr uint32_t kBoardRevision = 42;

  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.board_revision = kBoardRevision;

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "misc/sysinfo", &fd));

  fzl::FdioCaller caller(std::move(fd));
  auto result = ::llcpp::fuchsia::sysinfo::Device::Call::GetBoardRevision(caller.channel());
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(result.value().revision, kBoardRevision);
}

}  // namespace
