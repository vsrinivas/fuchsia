// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests run with an external network interface providing default route
// addresses.

#include <arpa/inet.h>

#include "gtest/gtest.h"

namespace {

TEST(ExternalNetworkTest, ConnectToNonRoutableINET) {
  int s;
  ASSERT_GE(s = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0), 0)
      << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;

  // RFC5737#section-3
  //
  // The blocks 192.0.2.0/24 (TEST-NET-1), 198.51.100.0/24 (TEST-NET-2),and
  // 203.0.113.0/24 (TEST-NET-3) are provided for use in documentation.
  ASSERT_EQ(inet_pton(AF_INET, "192.0.2.55", &addr.sin_addr), 1)
      << strerror(errno);

  addr.sin_port = htons(1337);

  ASSERT_EQ(
      connect(s, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
      -1);
  ASSERT_EQ(errno, EINPROGRESS) << strerror(errno);

  ASSERT_EQ(close(s), 0) << strerror(errno);
}

TEST(ExternalNetworkTest, ConnectToNonRoutableINET6) {
  int s;
  ASSERT_GE(s = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0), 0)
      << strerror(errno);

  struct sockaddr_in6 addr = {};
  addr.sin6_family = AF_INET6;

  // RFC3849#section-2
  //
  // The prefix allocated for documentation purposes is 2001:DB8::/32.
  ASSERT_EQ(inet_pton(AF_INET6, "2001:db8::55", &addr.sin6_addr), 1)
      << strerror(errno);

  addr.sin6_port = htons(1337);

  ASSERT_EQ(
      connect(s, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
      -1);

// If host test env does not support ipv6, the errno is set to ENETUNREACH.
// TODO(sshrivy): See if there's a way to detect this in program and assert
// accordingly.
#if defined(__linux__)
  ASSERT_TRUE(errno == EINPROGRESS || errno == ENETUNREACH) << strerror(errno);
#else
  ASSERT_EQ(errno, EINPROGRESS) << strerror(errno);
#endif

  ASSERT_EQ(close(s), 0) << strerror(errno);
}

}  // namespace
