// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/inet/ip_address.h"

#include <gtest/gtest.h>

namespace inet {
namespace test {

// Tests the properties of an invalid address.
TEST(IpAddressTest, Invalid) {
  IpAddress under_test;
  EXPECT_FALSE(under_test.is_valid());
  EXPECT_EQ(AF_UNSPEC, under_test.family());
  EXPECT_FALSE(under_test.is_v4());
  EXPECT_FALSE(under_test.is_v6());
  EXPECT_FALSE(under_test.is_loopback());
  EXPECT_FALSE(under_test.is_link_local());
  EXPECT_EQ("<invalid>", under_test.ToString());
  EXPECT_EQ(IpAddress::kInvalid, under_test);
}

// Tests the properties of a V4 address.
TEST(IpAddressTest, V4) {
  IpAddress under_test(1, 2, 3, 4);
  EXPECT_TRUE(under_test.is_valid());
  EXPECT_EQ(AF_INET, under_test.family());
  EXPECT_TRUE(under_test.is_v4());
  EXPECT_FALSE(under_test.is_v6());
  EXPECT_FALSE(under_test.is_loopback());
  EXPECT_FALSE(under_test.is_link_local());
  EXPECT_EQ(0x04030201u, under_test.as_in_addr().s_addr);
  EXPECT_EQ(0x04030201u, under_test.as_in_addr_t());
  EXPECT_EQ(1u, under_test.as_bytes()[0]);
  EXPECT_EQ(2u, under_test.as_bytes()[1]);
  EXPECT_EQ(3u, under_test.as_bytes()[2]);
  EXPECT_EQ(4u, under_test.as_bytes()[3]);
  EXPECT_EQ(0x0201u, under_test.as_words()[0]);
  EXPECT_EQ(0x0403u, under_test.as_words()[1]);
  EXPECT_EQ(4u, under_test.byte_count());
  EXPECT_EQ(2u, under_test.word_count());
  EXPECT_EQ("1.2.3.4", under_test.ToString());
}

// Tests the properties of a V6 address.
TEST(IpAddressTest, V6) {
  IpAddress under_test(0x0001, 0x0203, 0x0405, 0x0607, 0x0809, 0x0a0b, 0x0c0d, 0x0e0f);
  EXPECT_TRUE(under_test.is_valid());
  EXPECT_EQ(AF_INET6, under_test.family());
  EXPECT_FALSE(under_test.is_v4());
  EXPECT_TRUE(under_test.is_v6());
  EXPECT_FALSE(under_test.is_loopback());
  EXPECT_FALSE(under_test.is_link_local());

  for (size_t i = 0; i < 16; ++i) {
    EXPECT_EQ(i, under_test.as_in6_addr().s6_addr[i]);
    EXPECT_EQ(i, under_test.as_bytes()[i]);
  }

  for (size_t i = 0; i < 8; ++i) {
    EXPECT_EQ((i * 2u) + 256u * (i * 2u + 1u), under_test.as_words()[i]);
  }

  EXPECT_EQ(16u, under_test.byte_count());
  EXPECT_EQ(8u, under_test.word_count());
  EXPECT_EQ("1:203:405:607:809:a0b:c0d:e0f", under_test.ToString());
}

// Tests constructors.
TEST(IpAddressTest, Constructors) {
  IpAddress v4(1, 2, 3, 4);
  IpAddress v6(0x1234, 0, 0, 0, 0, 0, 0, 0x5678);

  EXPECT_EQ(v4, IpAddress(v4.as_in_addr_t()));
  EXPECT_EQ(v4, IpAddress(v4.as_in_addr()));
  sockaddr_storage sockaddr_v4{.ss_family = AF_INET};
  memcpy(reinterpret_cast<uint8_t*>(&sockaddr_v4) + sizeof(sa_family_t), v4.as_bytes(),
         v4.byte_count());
  EXPECT_EQ(v4, IpAddress(*reinterpret_cast<sockaddr*>(&sockaddr_v4)));
  EXPECT_EQ(v4, IpAddress(sockaddr_v4));
  fuchsia::net::IpAddress fn_ip_address_v4;
  fn_ip_address_v4.set_ipv4(fuchsia::net::Ipv4Address{.addr = {1, 2, 3, 4}});
  EXPECT_EQ(v4, IpAddress(fn_ip_address_v4));

  EXPECT_EQ(v6, IpAddress(0x1234, 0x5678));
  EXPECT_EQ(v6, IpAddress(v6.as_in6_addr()));
  sockaddr_storage sockaddr_v6{.ss_family = AF_INET6};
  memcpy(reinterpret_cast<uint8_t*>(&sockaddr_v6) + sizeof(sa_family_t), v6.as_bytes(),
         v6.byte_count());
  EXPECT_EQ(v6, IpAddress(*reinterpret_cast<sockaddr*>(&sockaddr_v6)));
  EXPECT_EQ(v6, IpAddress(sockaddr_v6));
  fuchsia::net::IpAddress fn_ip_address_v6;
  fn_ip_address_v6.set_ipv6(fuchsia::net::Ipv6Address{
      .addr = {0x12, 0x34, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x56, 0x78}});
  EXPECT_EQ(v6, IpAddress(fn_ip_address_v6));
}

// Tests is_loopback method.
TEST(IpAddressTest, IsLoopback) {
  EXPECT_FALSE(IpAddress::kInvalid.is_loopback());
  EXPECT_FALSE(IpAddress(1, 2, 3, 4).is_loopback());
  EXPECT_FALSE(IpAddress(0x1234, 0x5678).is_loopback());
  EXPECT_TRUE(IpAddress::kV4Loopback.is_loopback());
  EXPECT_TRUE(IpAddress::kV6Loopback.is_loopback());
}

// Tests is_link_local method.
TEST(IpAddressTest, IsLinkLocal) {
  EXPECT_FALSE(IpAddress::kInvalid.is_link_local());
  EXPECT_FALSE(IpAddress(1, 2, 3, 4).is_link_local());
  EXPECT_FALSE(IpAddress(0x1234, 0x5678).is_link_local());
  EXPECT_FALSE(IpAddress::kV4Loopback.is_link_local());
  EXPECT_FALSE(IpAddress::kV6Loopback.is_link_local());
  EXPECT_FALSE(IpAddress(168, 254, 0, 0).is_link_local());
  EXPECT_FALSE(IpAddress(170, 254, 0, 0).is_link_local());
  EXPECT_FALSE(IpAddress(169, 253, 0, 0).is_link_local());
  EXPECT_FALSE(IpAddress(169, 255, 0, 0).is_link_local());
  EXPECT_TRUE(IpAddress(169, 254, 0, 0).is_link_local());
  EXPECT_TRUE(IpAddress(169, 254, 255, 255).is_link_local());
  EXPECT_TRUE(IpAddress(169, 254, 0, 0).is_link_local());
  EXPECT_TRUE(IpAddress(169, 254, 255, 0).is_link_local());
  EXPECT_TRUE(IpAddress(169, 254, 0, 255).is_link_local());
  EXPECT_FALSE(IpAddress(0xfec0, 0x1234).is_link_local());
  EXPECT_FALSE(IpAddress(0xfe40, 0x1234).is_link_local());
  EXPECT_TRUE(IpAddress(0xfe80, 0x0).is_link_local());
  EXPECT_TRUE(IpAddress(0xfe80, 0xffff).is_link_local());
}

// Tests FromString static method.
TEST(IpAddressTest, FromString) {
  EXPECT_EQ(IpAddress(1, 2, 3, 4), IpAddress::FromString("1.2.3.4"));
  EXPECT_EQ(IpAddress(1, 2, 3, 4), IpAddress::FromString("001.002.003.004"));
  EXPECT_EQ(IpAddress(0, 0, 0, 0), IpAddress::FromString("0.0.0.0"));
  EXPECT_EQ(IpAddress(255, 255, 255, 255), IpAddress::FromString("255.255.255.255"));

  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2.3"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2.3.4.5"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2.3.4.5.6"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("0001.2.3.4"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2.3..4"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2.3.4."));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString(".1.2.3.4"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("256.2.3.4"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1234.2.3.4"));

  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2:3:4:5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1::3:4:5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0, 0x04, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2::4:5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2:3::5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2:3:4::6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0, 0x07, 0x08),
            IpAddress::FromString("1:2:3:4:5::7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0, 0x08),
            IpAddress::FromString("1:2:3:4:5:6::8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0, 0x04, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1::4:5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0, 0, 0x05, 0x06, 0x07, 0x08), IpAddress::FromString("1::5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0, 0, 0, 0x06, 0x07, 0x08), IpAddress::FromString("1::6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0, 0, 0, 0, 0x07, 0x08), IpAddress::FromString("1::7:8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0, 0, 0, 0, 0, 0x08), IpAddress::FromString("1::8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0, 0, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2::5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0, 0, 0, 0x06, 0x07, 0x08), IpAddress::FromString("1:2::6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0, 0, 0, 0, 0x07, 0x08), IpAddress::FromString("1:2::7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0, 0, 0, 0, 0, 0x08), IpAddress::FromString("1:2::8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0, 0, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2:3::6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0, 0, 0, 0x07, 0x08), IpAddress::FromString("1:2:3::7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0, 0, 0, 0, 0x08), IpAddress::FromString("1:2:3::8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0, 0, 0x07, 0x08),
            IpAddress::FromString("1:2:3:4::7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0x08), IpAddress::FromString("1:2:3:4::8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0, 0, 0x08),
            IpAddress::FromString("1:2:3:4:5::8"));
  EXPECT_EQ(IpAddress(0x1234, 0x5678, 0x9abc, 0xdef0, 0x0fed, 0xcba9, 0x8765, 0x4321),
            IpAddress::FromString("1234:5678:9abc:def0:0fed:cba9:8765:4321"));
  EXPECT_EQ(IpAddress(0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff),
            IpAddress::FromString("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
  EXPECT_EQ(IpAddress(0, 0, 0, 0, 0, 0, 0, 1), IpAddress::FromString("::1"));
  EXPECT_EQ(IpAddress(1, 0, 0, 0, 0, 0, 0, 0), IpAddress::FromString("1::"));
  EXPECT_EQ(IpAddress(0, 0, 0, 0, 0, 0, 0, 0), IpAddress::FromString("::"));

  // Regression test for fxb/103890.
  EXPECT_EQ(IpAddress(0xb043, 0x5c50, 0x7f6a, 0xd804, 0x9eff, 0x47df, 0, 0),
            IpAddress::FromString("b043:5c50:7f6a:d804:9eff:47df::"));

  // Allow uppercase hexadecimal.
  EXPECT_EQ(IpAddress(0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x0FED, 0xCBA9, 0x8765, 0x4321),
            IpAddress::FromString("1234:5678:9abc:def0:0fed:cba9:8765:4321"));

  // Allow zeros adjacent to '::'.
  EXPECT_EQ(IpAddress(0, 0, 0, 0, 0, 0, 0, 0x08), IpAddress::FromString("0:0:0:0:0:0::8"));

  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:::2"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1::2::3"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString(":1::2"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1::2:"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("00000::ffff"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("0000::fffff"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4:5:6:7:8:9"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4:5:6:7:8:"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString(":1:2:3:4:5:6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1::2:3:4:5:6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2::3:4:5:6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3::4:5:6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4::5:6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4:5::6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4:5:6::7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4:5:6:7::8"));

  // Test |family| parameter.
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4:5:6:7:8", AF_INET));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2.3.4", AF_INET6));
}

// Tests FromStringView static method.
TEST(IpAddressTest, FromStringView) {
  EXPECT_EQ(std::make_pair(IpAddress(1, 2, 3, 4), 7ul), IpAddress::FromStringView("1.2.3.4"));
  EXPECT_EQ(std::make_pair(IpAddress(1, 2, 3, 4), 15ul),
            IpAddress::FromStringView("001.002.003.004"));
  EXPECT_EQ(std::make_pair(IpAddress(0, 0, 0, 0), 7ul), IpAddress::FromStringView("0.0.0.0"));
  EXPECT_EQ(std::make_pair(IpAddress(255, 255, 255, 255), 15ul),
            IpAddress::FromStringView("255.255.255.255"));

  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul), IpAddress::FromStringView("1"));
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul), IpAddress::FromStringView("1.2"));
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul), IpAddress::FromStringView("1.2.3"));
  EXPECT_EQ(std::make_pair(IpAddress(1, 2, 3, 4), 7ul), IpAddress::FromStringView("1.2.3.4.5"));
  EXPECT_EQ(std::make_pair(IpAddress(1, 2, 3, 4), 7ul), IpAddress::FromStringView("1.2.3.4.5.6"));
  EXPECT_EQ(std::make_pair(IpAddress(1, 2, 3, 4), 7ul), IpAddress::FromStringView("1.2.3.4foo"));
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul), IpAddress::FromStringView("0001.2.3.4"));
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul), IpAddress::FromStringView("1.2.3..4"));
  EXPECT_EQ(std::make_pair(IpAddress(1, 2, 3, 4), 7ul), IpAddress::FromStringView("1.2.3.4."));
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul), IpAddress::FromStringView(".1.2.3.4"));
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul), IpAddress::FromStringView("256.2.3.4"));
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul), IpAddress::FromStringView("1234.2.3.4"));

  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08), 15ul),
            IpAddress::FromStringView("1:2:3:4:5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08), 14ul),
            IpAddress::FromStringView("1::3:4:5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0, 0x04, 0x05, 0x06, 0x07, 0x08), 14ul),
            IpAddress::FromStringView("1:2::4:5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0, 0x05, 0x06, 0x07, 0x08), 14ul),
            IpAddress::FromStringView("1:2:3::5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0, 0x06, 0x07, 0x08), 14ul),
            IpAddress::FromStringView("1:2:3:4::6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0, 0x07, 0x08), 14ul),
            IpAddress::FromStringView("1:2:3:4:5::7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0, 0x08), 14ul),
            IpAddress::FromStringView("1:2:3:4:5:6::8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0, 0, 0x04, 0x05, 0x06, 0x07, 0x08), 12ul),
            IpAddress::FromStringView("1::4:5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0, 0, 0, 0x05, 0x06, 0x07, 0x08), 10ul),
            IpAddress::FromStringView("1::5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0, 0, 0, 0, 0x06, 0x07, 0x08), 8ul),
            IpAddress::FromStringView("1::6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0, 0, 0, 0, 0, 0x07, 0x08), 6ul),
            IpAddress::FromStringView("1::7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0, 0, 0, 0, 0, 0, 0x08), 4ul),
            IpAddress::FromStringView("1::8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0, 0, 0x05, 0x06, 0x07, 0x08), 12ul),
            IpAddress::FromStringView("1:2::5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0, 0, 0, 0x06, 0x07, 0x08), 10ul),
            IpAddress::FromStringView("1:2::6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0, 0, 0, 0, 0x07, 0x08), 8ul),
            IpAddress::FromStringView("1:2::7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0, 0, 0, 0, 0, 0x08), 6ul),
            IpAddress::FromStringView("1:2::8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0, 0, 0x06, 0x07, 0x08), 12ul),
            IpAddress::FromStringView("1:2:3::6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0, 0, 0, 0x07, 0x08), 10ul),
            IpAddress::FromStringView("1:2:3::7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0, 0, 0, 0, 0x08), 8ul),
            IpAddress::FromStringView("1:2:3::8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0, 0, 0x07, 0x08), 12ul),
            IpAddress::FromStringView("1:2:3:4::7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0x08), 10ul),
            IpAddress::FromStringView("1:2:3:4::8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0, 0, 0x08), 12ul),
            IpAddress::FromStringView("1:2:3:4:5::8"));
  EXPECT_EQ(std::make_pair(
                IpAddress(0x1234, 0x5678, 0x9abc, 0xdef0, 0x0fed, 0xcba9, 0x8765, 0x4321), 39ul),
            IpAddress::FromStringView("1234:5678:9abc:def0:0fed:cba9:8765:4321"));
  EXPECT_EQ(std::make_pair(
                IpAddress(0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff), 39ul),
            IpAddress::FromStringView("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
  EXPECT_EQ(std::make_pair(IpAddress(0, 0, 0, 0, 0, 0, 0, 1), 3ul),
            IpAddress::FromStringView("::1"));
  EXPECT_EQ(std::make_pair(IpAddress(1, 0, 0, 0, 0, 0, 0, 0), 3ul),
            IpAddress::FromStringView("1::"));
  EXPECT_EQ(std::make_pair(IpAddress(0, 0, 0, 0, 0, 0, 0, 0), 2ul),
            IpAddress::FromStringView("::"));

  // Regression test for fxb/103890.
  EXPECT_EQ(std::make_pair(IpAddress(0xb043, 0x5c50, 0x7f6a, 0xd804, 0x9eff, 0x47df, 0, 0), 31ul),
            IpAddress::FromStringView("b043:5c50:7f6a:d804:9eff:47df::"));

  // Allow uppercase hexadecimal.
  EXPECT_EQ(std::make_pair(
                IpAddress(0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x0FED, 0xCBA9, 0x8765, 0x4321), 39ul),
            IpAddress::FromStringView("1234:5678:9abc:def0:0fed:cba9:8765:4321"));

  // Allow zeros adjacent to '::'.
  EXPECT_EQ(std::make_pair(IpAddress(0, 0, 0, 0, 0, 0, 0, 0x08), 14ul),
            IpAddress::FromStringView("0:0:0:0:0:0::8"));

  EXPECT_EQ(std::make_pair(IpAddress(1, 0, 0, 0, 0, 0, 0, 0), 3ul),
            IpAddress::FromStringView("1:::2"));
  EXPECT_EQ(std::make_pair(IpAddress(1, 0, 0, 0, 0, 0, 0, 2), 4ul),
            IpAddress::FromStringView("1::2::3"));
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul), IpAddress::FromStringView(":1::2"));
  EXPECT_EQ(std::make_pair(IpAddress(1, 0, 0, 0, 0, 0, 0, 2), 4ul),
            IpAddress::FromStringView("1::2:"));
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul), IpAddress::FromStringView("00000::ffff"));
  EXPECT_EQ(std::make_pair(IpAddress(0, 0, 0, 0, 0, 0, 0, 0xffff), 10ul),
            IpAddress::FromStringView("0000::fffff"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08), 15ul),
            IpAddress::FromStringView("1:2:3:4:5:6:7:8:9"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08), 15ul),
            IpAddress::FromStringView("1:2:3:4:5:6:7:8:"));
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul),
            IpAddress::FromStringView(":1:2:3:4:5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07), 14ul),
            IpAddress::FromStringView("1::2:3:4:5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x00, 0x03, 0x04, 0x05, 0x06, 0x07), 14ul),
            IpAddress::FromStringView("1:2::3:4:5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x00, 0x04, 0x05, 0x06, 0x07), 14ul),
            IpAddress::FromStringView("1:2:3::4:5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0x00, 0x05, 0x06, 0x07), 14ul),
            IpAddress::FromStringView("1:2:3:4::5:6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0x00, 0x06, 0x07), 14ul),
            IpAddress::FromStringView("1:2:3:4:5::6:7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x00, 0x07), 14ul),
            IpAddress::FromStringView("1:2:3:4:5:6::7:8"));
  EXPECT_EQ(std::make_pair(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00), 15ul),
            IpAddress::FromStringView("1:2:3:4:5:6:7::8"));

  // Test |family| parameter.
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul),
            IpAddress::FromStringView("1:2:3:4:5:6:7:8", AF_INET));
  EXPECT_EQ(std::make_pair(IpAddress::kInvalid, 0ul),
            IpAddress::FromStringView("1.2.3.4", AF_INET6));
}

// Tests FromString and ToString against each other.
TEST(IpAddressTest, StringRoundTrip) {
#if !defined(__Fuchsia__)
  std::srand(testing::UnitTest::GetInstance()->random_seed());
#endif

  for (size_t i = 0; i < 1000; ++i) {
    struct in_addr addr;
    struct in6_addr addr6;

#if defined(__Fuchsia__)
    zx_cprng_draw(&addr, sizeof(addr));
    zx_cprng_draw(&addr6, sizeof(addr6));
#else
    addr.s_addr = std::rand();
    for (auto& i : addr6.s6_addr32) {
      i = std::rand();
    }
#endif

    IpAddress v4(addr);
    EXPECT_EQ(v4, IpAddress::FromString(v4.ToString()));

    IpAddress v6(addr6);
    EXPECT_EQ(v6, IpAddress::FromString(v6.ToString()));
  }
}

// Tests FromStringView and ToString against each other.
TEST(IpAddressTest, StringViewRoundTrip) {
#if !defined(__Fuchsia__)
  std::srand(testing::UnitTest::GetInstance()->random_seed());
#endif

  for (size_t i = 0; i < 1000; ++i) {
    struct in_addr addr;
    struct in6_addr addr6;

#if defined(__Fuchsia__)
    zx_cprng_draw(&addr, sizeof(addr));
    zx_cprng_draw(&addr6, sizeof(addr6));
#else
    addr.s_addr = std::rand();
    for (auto& i : addr6.s6_addr32) {
      i = std::rand();
    }
#endif

    IpAddress v4(addr);
    std::string v4_string = v4.ToString();
    EXPECT_EQ(std::make_pair(v4, v4_string.size()), IpAddress::FromStringView(v4_string));

    IpAddress v6(addr6);
    std::string v6_string = v6.ToString();
    EXPECT_EQ(std::make_pair(v6, v6_string.size()), IpAddress::FromStringView(v6_string));
  }
}

// Tests |is_mapped_from_v4|, |mapped_v4_address|, and |mapped_as_v6|.
TEST(IpAddressTest, MappedV4Address) {
  EXPECT_FALSE(IpAddress(1, 2, 3, 4).is_mapped_from_v4());
  EXPECT_FALSE(IpAddress(0x1234, 0, 0, 0, 0, 0, 0, 0x5678).is_mapped_from_v4());
  EXPECT_FALSE(IpAddress::FromString("0::fffe:0:0").is_mapped_from_v4());
  EXPECT_FALSE(IpAddress::FromString("0::ffef:0:0").is_mapped_from_v4());
  EXPECT_FALSE(IpAddress::FromString("0::feff:0:0").is_mapped_from_v4());
  EXPECT_FALSE(IpAddress::FromString("0::efff:0:0").is_mapped_from_v4());
  EXPECT_TRUE(IpAddress::FromString("0::ffff:0:0").is_mapped_from_v4());
  EXPECT_EQ(IpAddress(0, 0, 0, 0), IpAddress::FromString("0::ffff:0:0").mapped_v4_address());
  EXPECT_EQ(IpAddress(1, 2, 3, 4), IpAddress::FromString("0::ffff:102:304").mapped_v4_address());
  EXPECT_EQ(IpAddress::FromString("0::ffff:102:304"), IpAddress(1, 2, 3, 4).mapped_as_v6());
}

}  // namespace test
}  // namespace inet
