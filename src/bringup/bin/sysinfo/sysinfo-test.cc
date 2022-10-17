// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <lib/sys/component/cpp/service_client.h>

#include <zxtest/zxtest.h>

namespace sysinfo {

TEST(SysinfoTest, GetBoardName) {
  // Get the resource handle from the driver.
  zx::result client_end = component::Connect<fuchsia_sysinfo::SysInfo>();
  ASSERT_OK(client_end.status_value());

  // Test fuchsia::sysinfo::SysInfo.GetBoardName().
  const fidl::WireResult result = fidl::WireCall(client_end.value())->GetBoardName();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  ASSERT_FALSE(response.name.empty());
}

TEST(SysinfoTest, GetBoardRevision) {
  // Get the resource handle from the driver.
  zx::result client_end = component::Connect<fuchsia_sysinfo::SysInfo>();
  ASSERT_OK(client_end.status_value());

  // Test fuchsia::sysinfo::SysInfo.GetBoardRevision().
  const fidl::WireResult result = fidl::WireCall(client_end.value())->GetBoardRevision();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
}

TEST(SysinfoTest, GetBootloaderVendor) {
  // Get the resource handle from the driver.
  zx::result client_end = component::Connect<fuchsia_sysinfo::SysInfo>();
  ASSERT_OK(client_end.status_value());

  // Test fuchsia::sysinfo::SysInfo.GetBootloaderVendor().
  const fidl::WireResult result = fidl::WireCall(client_end.value())->GetBootloaderVendor();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
}

TEST(SysinfoTest, GetInterruptControllerInfo) {
  // Get the resource handle from the driver.
  zx::result client_end = component::Connect<fuchsia_sysinfo::SysInfo>();
  ASSERT_OK(client_end.status_value());

  // Test fuchsia::sysinfo::SysInfo.GetInterruptControllerInfo().
  const fidl::WireResult result = fidl::WireCall(client_end.value())->GetInterruptControllerInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  ASSERT_NOT_NULL(response.info.get(), "interrupt controller type is unknown");
  EXPECT_NE(response.info->type, fuchsia_sysinfo::wire::InterruptControllerType::kUnknown,
            "interrupt controller type is unknown");
}

}  // namespace sysinfo
