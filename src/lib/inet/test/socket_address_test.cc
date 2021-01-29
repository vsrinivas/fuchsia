// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/inet/socket_address.h"

#include <gtest/gtest.h>

namespace inet {
namespace test {

TEST(SocketAddressTest, BuildFromFIDL) {
  auto fnet_v4 = fuchsia::net::SocketAddress::WithIpv4(
      fuchsia::net::Ipv4SocketAddress{fuchsia::net::Ipv4Address{{192, 168, 0, 1}}, 80});
  SocketAddress sockaddr_v4(IpAddress(192, 168, 0, 1), IpPort::From_uint16_t(80));

  EXPECT_EQ(SocketAddress(fnet_v4), sockaddr_v4);
  EXPECT_EQ(SocketAddress(fnet_v4.ipv4()), sockaddr_v4);

  auto fnet_v6 = fuchsia::net::SocketAddress::WithIpv6(fuchsia::net::Ipv6SocketAddress{
      fuchsia::net::Ipv6Address{{0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc4, 0xed, 0xaa,
                                 0x12, 0x16, 0x78, 0xf6, 0x79}},
      80, 1});
  SocketAddress sockaddr_v6(
      IpAddress(0xfe80, 0x0000, 0x000, 0x0000, 0xc4ed, 0xaa12, 0x1678, 0xf679),
      IpPort::From_uint16_t(80), 1);

  EXPECT_EQ(SocketAddress(fnet_v6), sockaddr_v6);
  EXPECT_EQ(SocketAddress(fnet_v6.ipv6()), sockaddr_v6);
}

}  // namespace test
}  // namespace inet
