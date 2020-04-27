// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests should run without any network interface (except loopback).

#include <arpa/inet.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <limits.h>
#include <sys/utsname.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

namespace {

TEST(NameProviderTest, GetHostNameDefault) {
  char hostname[HOST_NAME_MAX];
  ASSERT_EQ(gethostname(hostname, sizeof(hostname)), 0) << strerror(errno);
  ASSERT_STREQ(hostname, llcpp::fuchsia::device::DEFAULT_DEVICE_NAME);
}

TEST(NameProviderTest, UnameDefault) {
  utsname uts;
  ASSERT_EQ(uname(&uts), 0) << strerror(errno);
  ASSERT_STREQ(uts.nodename, llcpp::fuchsia::device::DEFAULT_DEVICE_NAME);
}

TEST(NameProviderTest, GetDeviceName) {
  zx::channel c0, c1;
  zx_status_t status;

  ASSERT_EQ(status = zx::channel::create(0, &c0, &c1), ZX_OK) << zx_status_get_string(status);
  ASSERT_EQ(status = sys::ServiceDirectory::CreateFromNamespace()->Connect(
                llcpp::fuchsia::device::NameProvider::Name, std::move(c1)),
            ZX_OK)
      << zx_status_get_string(status);

  llcpp::fuchsia::device::NameProvider::SyncClient name_provider(std::move(c0));
  auto response = name_provider.GetDeviceName();
  ASSERT_EQ(status = response.status(), ZX_OK) << zx_status_get_string(status);
  auto result = std::move(response.Unwrap()->result);

  ASSERT_TRUE(!result.is_err()) << zx_status_get_string(result.err());
  auto& name = result.response().name;

  // regression test: ensure that no additional data is present past the last null byte
  EXPECT_EQ(name.size(), strlen(llcpp::fuchsia::device::DEFAULT_DEVICE_NAME));
  EXPECT_EQ(memcmp(name.data(), llcpp::fuchsia::device::DEFAULT_DEVICE_NAME, name.size()), 0);
}

}  // namespace
