// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/inet_util.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(InetUtil, ParseHostPort) {
  std::string host;
  uint16_t port;

  // Host good.
  EXPECT_FALSE(ParseHostPort("google.com:1234", &host, &port).has_error());
  EXPECT_EQ("google.com", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("google.com", "1234", &host, &port).has_error());
  EXPECT_EQ("google.com", host);
  EXPECT_EQ(1234, port);

  // IPv4 Good.
  EXPECT_FALSE(ParseHostPort("192.168.0.1:1234", &host, &port).has_error());
  EXPECT_EQ("192.168.0.1", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("192.168.0.1", "1234", &host, &port).has_error());
  EXPECT_EQ("192.168.0.1", host);
  EXPECT_EQ(1234, port);

  // IPv6 Good.
  EXPECT_FALSE(ParseHostPort("[1234::5678]:1234", &host, &port).has_error());
  EXPECT_EQ("1234::5678", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("[1234::5678]", "1234", &host, &port).has_error());
  EXPECT_EQ("1234::5678", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("1234::5678", "1234", &host, &port).has_error());
  EXPECT_EQ("1234::5678", host);
  EXPECT_EQ(1234, port);

  // Missing ports.
  EXPECT_TRUE(ParseHostPort("google.com", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("192.168.0.1", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("1234::5678", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("[1234::5678]", &host, &port).has_error());

  // Bad port values.
  EXPECT_TRUE(ParseHostPort("google.com:0", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("google.com:99999999", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("google.com:-1", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("google.com:fubar", &host, &port).has_error());
}

TEST(InetUtil, Ipv6HostPortIsMissingBrackets) {
  EXPECT_TRUE(Ipv6HostPortIsMissingBrackets("1234::5678"));
  EXPECT_TRUE(Ipv6HostPortIsMissingBrackets("1234::5678:22"));

  EXPECT_TRUE(Ipv6HostPortIsMissingBrackets("[1234::5678:22"));
  EXPECT_TRUE(Ipv6HostPortIsMissingBrackets("1234::5678]:22"));

  EXPECT_FALSE(Ipv6HostPortIsMissingBrackets("[1234::5678]:22"));

  EXPECT_FALSE(Ipv6HostPortIsMissingBrackets(""));
  EXPECT_FALSE(Ipv6HostPortIsMissingBrackets(":1234"));
  EXPECT_FALSE(Ipv6HostPortIsMissingBrackets("google.com"));
  EXPECT_FALSE(Ipv6HostPortIsMissingBrackets("google.com:1234"));
}

}  // namespace zxdb
