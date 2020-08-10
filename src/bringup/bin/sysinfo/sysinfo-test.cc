// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>
#include <unistd.h>
#include <zircon/boot/image.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <array>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

using llcpp::fuchsia::sysinfo::InterruptControllerType;
using llcpp::fuchsia::sysinfo::SysInfo;

namespace sysinfo {

namespace {

const std::string kSysinfoPath = "/svc/" + std::string(SysInfo::Name);

}  // namespace

TEST(SysinfoTest, GetBoardName) {
  // Get the resource handle from the driver.
  fbl::unique_fd fd(open(kSysinfoPath.c_str(), O_RDWR));
  ASSERT_TRUE(fd.is_valid(), "Can't open sysinfo");

  zx::channel channel;
  ASSERT_OK(fdio_get_service_handle(fd.release(), channel.reset_and_get_address()),
            "Failed to get channel");
  SysInfo::SyncClient sysinfo(std::move(channel));

  // Test fuchsia::sysinfo::SysInfo.GetBoardName().
  auto result = sysinfo.GetBoardName();
  ASSERT_TRUE(result.ok(), "Failed to get board name");
  ASSERT_OK(result->status, "Failed to get board name");
  ASSERT_GT(result->name.size(), 0, "board name is empty");
}

TEST(SysinfoTest, GetBoardRevision) {
  // Get the resource handle from the driver.
  fbl::unique_fd fd(open(kSysinfoPath.c_str(), O_RDWR));
  ASSERT_TRUE(fd.is_valid(), "Can't open sysinfo");

  zx::channel channel;
  ASSERT_OK(fdio_get_service_handle(fd.release(), channel.reset_and_get_address()),
            "Failed to get channel");
  SysInfo::SyncClient sysinfo(std::move(channel));

  // Test fuchsia::sysinfo::SysInfo.GetBoardRevision().
  auto result = sysinfo.GetBoardRevision();
  ASSERT_TRUE(result.ok(), "Failed to get board revision");
  ASSERT_OK(result->status, "Failed to get board revision");
}

TEST(SysinfoTest, GetBootloaderVendor) {
  // Get the resource handle from the driver.
  fbl::unique_fd fd(open(kSysinfoPath.c_str(), O_RDWR));
  ASSERT_TRUE(fd.is_valid(), "Can't open sysinfo");

  zx::channel channel;
  ASSERT_OK(fdio_get_service_handle(fd.release(), channel.reset_and_get_address()),
            "Failed to get channel");
  SysInfo::SyncClient sysinfo(std::move(channel));

  // Test fuchsia::sysinfo::SysInfo.GetBootloaderVendor().
  auto result = sysinfo.GetBootloaderVendor();
  ASSERT_TRUE(result.ok(), "Failed to get bootloader vendor");
  ASSERT_OK(result->status, "Failed to get bootloader vendor");
}

TEST(SysinfoTest, GetInterruptControllerInfo) {
  // Get the resource handle from the driver.
  fbl::unique_fd fd(open(kSysinfoPath.c_str(), O_RDWR));
  ASSERT_TRUE(fd.is_valid(), "Can't open sysinfo");

  zx::channel channel;
  ASSERT_OK(fdio_get_service_handle(fd.release(), channel.reset_and_get_address()),
            "Failed to get channel");
  SysInfo::SyncClient sysinfo(std::move(channel));

  // Test fuchsia::sysinfo::SysInfo.GetInterruptControllerInfo().
  auto result = sysinfo.GetInterruptControllerInfo();
  ASSERT_TRUE(result.ok(), "Failed to get interrupt controller info");
  ASSERT_OK(result->status, "Failed to get interrupt controller info");
  ASSERT_NOT_NULL(result->info.get(), "interrupt controller type is unknown");
  EXPECT_NE(result->info->type, InterruptControllerType::UNKNOWN,
            "interrupt controller type is unknown");
}

TEST(SysinfoTest, GetHypervisorResource) {
  // Get the resource handle from the driver.
  fbl::unique_fd fd(open(kSysinfoPath.c_str(), O_RDWR));
  ASSERT_TRUE(fd.is_valid(), "Can't open sysinfo");

  zx::channel channel;
  ASSERT_OK(fdio_get_service_handle(fd.release(), channel.reset_and_get_address()),
            "Failed to get channel");
  SysInfo::SyncClient sysinfo(std::move(channel));

  // Test fuchsia::sysinfo::SysInfo.GetHypervisorResource().
  auto result = sysinfo.GetHypervisorResource();
  ASSERT_TRUE(result.ok(), "Failed to get hypervisor resource");
  ASSERT_OK(result->status, "Failed to get hypervisor resource");
  EXPECT_NE(result->resource, ZX_HANDLE_INVALID, "hypervisor handle is invalid");
}

}  // namespace sysinfo
