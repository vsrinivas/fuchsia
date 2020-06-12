// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/connectivity/weave/weavestack/fidl/stack_utils.h"

#include <gtest/gtest.h>

namespace weavestack {

TEST(HostFromHostname, IpV4) {
  constexpr char kIpV4Host[] = "192.168.1.15";
  auto host = HostFromHostname(kIpV4Host);
  ASSERT_TRUE(host.is_ip_address());
  EXPECT_TRUE(host.ip_address().is_ipv4());
}

TEST(HostFromHostName, IpV6) {
  constexpr char kIpV6Host[] = "fdf8:f53b:82e4::53";
  auto host = HostFromHostname(kIpV6Host);
  ASSERT_TRUE(host.is_ip_address());
  EXPECT_TRUE(host.ip_address().is_ipv6());
}

TEST(HostFromHostName, Hostname) {
  constexpr char kHostnameHost[] = "host.example.com";
  auto host = HostFromHostname(kHostnameHost);
  ASSERT_TRUE(host.is_hostname());
  EXPECT_EQ(host.hostname(), kHostnameHost);
}

}  // namespace weavestack

