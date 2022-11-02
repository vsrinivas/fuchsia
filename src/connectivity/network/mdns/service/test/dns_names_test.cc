// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/common/mdns_names.h"

namespace mdns {
namespace test {

// Tests |LocalHostFullName|.
TEST(MdnsNamesTest, LocalHostFullName) {
  EXPECT_EQ("test.host.name.local.", MdnsNames::HostFullName("test.host.name"));
  EXPECT_EQ("test-host-name.local.", MdnsNames::HostFullName("test-host-name"));
}

// Tests |LocalServiceFullName|.
TEST(MdnsNamesTest, LocalServiceFullName) {
  EXPECT_EQ("_printer._tcp.local.", MdnsNames::ServiceFullName("_printer._tcp."));
  EXPECT_EQ("_fuchsia._udp.local.", MdnsNames::ServiceFullName("_fuchsia._udp."));
}

// Tests |LocalServiceSubtypeFullName|.
TEST(MdnsNamesTest, LocalServiceSubtypeFullName) {
  EXPECT_EQ("_color._sub._printer._tcp.local.",
            MdnsNames::ServiceSubtypeFullName("_printer._tcp.", "_color"));
  EXPECT_EQ("_nuc._sub._fuchsia._udp.local.",
            MdnsNames::ServiceSubtypeFullName("_fuchsia._udp.", "_nuc"));
}

// Tests |LocalInstanceFullName|.
TEST(MdnsNamesTest, LocalInstanceFullName) {
  EXPECT_EQ("Acme Splotchamatic._printer._tcp.local.",
            MdnsNames::InstanceFullName("Acme Splotchamatic", "_printer._tcp."));
  EXPECT_EQ("My Egg Timer._fuchsia._udp.local.",
            MdnsNames::InstanceFullName("My Egg Timer", "_fuchsia._udp."));
}

// Tests |SplitInstanceFullName|.
TEST(MdnsNamesTest, SplitInstanceFullName) {
  std::string instance_name;
  std::string service_name;

  EXPECT_TRUE(MdnsNames::SplitInstanceFullName("Acme Splotchamatic._printer._tcp.local.",
                                               &instance_name, &service_name));
  EXPECT_EQ("Acme Splotchamatic", instance_name);
  EXPECT_EQ("_printer._tcp.", service_name);

  EXPECT_TRUE(MdnsNames::SplitInstanceFullName("My Egg Timer._fuchsia._udp.local.", &instance_name,
                                               &service_name));
  EXPECT_EQ("My Egg Timer", instance_name);
  EXPECT_EQ("_fuchsia._udp.", service_name);

  // No local suffix.
  EXPECT_FALSE(MdnsNames::SplitInstanceFullName("Acme Splotchamatic._printer._tcp.", &instance_name,
                                                &service_name));

  // Just a service name.
  EXPECT_FALSE(
      MdnsNames::SplitInstanceFullName("_printer._tcp.local.", &instance_name, &service_name));

  // Zero-length instance name.
  EXPECT_FALSE(
      MdnsNames::SplitInstanceFullName("._printer._tcp.local.", &instance_name, &service_name));

  // Instance name almost too long.
  EXPECT_TRUE(MdnsNames::SplitInstanceFullName(
      "012345678901234567890123456789012345678901234567890123456789012._"
      "printer._tcp.local.",
      &instance_name, &service_name));
  EXPECT_EQ("012345678901234567890123456789012345678901234567890123456789012", instance_name);
  EXPECT_EQ("_printer._tcp.", service_name);

  // Instance name too long.
  EXPECT_FALSE(MdnsNames::SplitInstanceFullName(
      "0123456789012345678901234567890123456789012345678901234567890123._"
      "printer._tcp.local.",
      &instance_name, &service_name));
}

// Tests |MatchServiceName|.
TEST(MdnsNamesTest, MatchServiceName) {
  std::string subtype;

  EXPECT_TRUE(MdnsNames::MatchServiceName("_printer._tcp.local.", "_printer._tcp.", &subtype));
  EXPECT_EQ("", subtype);

  EXPECT_TRUE(MdnsNames::MatchServiceName("_fuchsia._udp.local.", "_fuchsia._udp.", &subtype));
  EXPECT_EQ("", subtype);

  EXPECT_TRUE(
      MdnsNames::MatchServiceName("_color._sub._printer._tcp.local.", "_printer._tcp.", &subtype));
  EXPECT_EQ("_color", subtype);

  EXPECT_TRUE(
      MdnsNames::MatchServiceName("_nuc._sub._fuchsia._udp.local.", "_fuchsia._udp.", &subtype));
  EXPECT_EQ("_nuc", subtype);

  // Wrong service type.
  EXPECT_FALSE(MdnsNames::MatchServiceName("_printer._tcp.local.", "_fuchsia._udp.", &subtype));

  // Wrong service type with subtype.
  EXPECT_FALSE(
      MdnsNames::MatchServiceName("_color._sub._printer._tcp.local.", "_fuchsia._udp.", &subtype));

  // No local suffix.
  EXPECT_FALSE(MdnsNames::MatchServiceName("_printer._tcp.", "_printer._tcp.", &subtype));

  // No local suffix with subtype.
  EXPECT_FALSE(
      MdnsNames::MatchServiceName("_color._sub._printer._tcp.", "_printer._tcp.", &subtype));

  // Zero-length subtype.
  EXPECT_FALSE(
      MdnsNames::MatchServiceName("._sub._printer._tcp.local.", "_printer._tcp.", &subtype));

  // Missing _sub.
  EXPECT_FALSE(
      MdnsNames::MatchServiceName("_color._printer._tcp.local.", "_printer._tcp.", &subtype));

  // Subtype almost too long.
  EXPECT_TRUE(MdnsNames::MatchServiceName(
      "012345678901234567890123456789012345678901234567890123456789012._sub._"
      "printer._tcp.local.",
      "_printer._tcp.", &subtype));
  EXPECT_EQ("012345678901234567890123456789012345678901234567890123456789012", subtype);

  // Subtype too long.
  EXPECT_FALSE(MdnsNames::MatchServiceName(
      "0123456789012345678901234567890123456789012345678901234567890123._sub._"
      "printer._tcp.local.",
      "_printer._tcp.", &subtype));
}

// Tests |IsValidHostName|.
TEST(MdnsNamesTest, IsValidHostName) {
  EXPECT_TRUE(MdnsNames::IsValidHostName("gopher"));
  EXPECT_TRUE(MdnsNames::IsValidHostName("gopher-cow-alpaca-racoon"));
  EXPECT_TRUE(MdnsNames::IsValidHostName("gopher.cow.alpaca.racoon"));
  EXPECT_TRUE(MdnsNames::IsValidHostName("g.c.a.r"));
  EXPECT_TRUE(MdnsNames::IsValidHostName(
      "012345678901234567890123456789012345678901234567890123456789012"));
  EXPECT_TRUE(
      MdnsNames::IsValidHostName("012345678901234567890123456789012345678901234567890123456789012."
                                 "012345678901234567890123456789012345678901234567890123456789012."
                                 "012345678901234567890123456789012345678901234567890123456789012."
                                 "0123456789012345678901234567890123456789012345678901234"));

  // Empty.
  EXPECT_FALSE(MdnsNames::IsValidHostName(""));

  // Empty labels.
  EXPECT_FALSE(MdnsNames::IsValidHostName("."));

  // Empty labels.
  EXPECT_FALSE(MdnsNames::IsValidHostName(".."));

  // Empty label.
  EXPECT_FALSE(MdnsNames::IsValidHostName("gopher."));

  // Empty label.
  EXPECT_FALSE(MdnsNames::IsValidHostName("gopher..cow"));

  // Too long.
  EXPECT_FALSE(
      MdnsNames::IsValidHostName("012345678901234567890123456789012345678901234567890123456789012."
                                 "012345678901234567890123456789012345678901234567890123456789012."
                                 "012345678901234567890123456789012345678901234567890123456789012."
                                 "01234567890123456789012345678901234567890123456789012345"));

  // Label too long.
  EXPECT_FALSE(MdnsNames::IsValidHostName(
      "0123456789012345678901234567890123456789012345678901234567890123"));
}

// Tests |IsValidServiceName|.
TEST(MdnsNamesTest, IsValidServiceName) {
  EXPECT_TRUE(MdnsNames::IsValidServiceName("_printer._tcp."));
  EXPECT_TRUE(MdnsNames::IsValidServiceName("_printer._udp."));
  EXPECT_TRUE(MdnsNames::IsValidServiceName("_._udp."));
  EXPECT_TRUE(MdnsNames::IsValidServiceName("_x._udp."));
  EXPECT_TRUE(MdnsNames::IsValidServiceName("_012345678901234._tcp."));

  // Empty.
  EXPECT_FALSE(MdnsNames::IsValidServiceName(""));

  // No termination.
  EXPECT_FALSE(MdnsNames::IsValidServiceName("_printer._tcp"));

  // Invalid transport.
  EXPECT_FALSE(MdnsNames::IsValidServiceName("_printer._qfc."));

  // Empty label.
  EXPECT_FALSE(MdnsNames::IsValidServiceName("._tcp."));

  // Label too long.
  EXPECT_FALSE(MdnsNames::IsValidServiceName("_0123456789012345._tcp."));

  // No leading underscore.
  EXPECT_FALSE(MdnsNames::IsValidServiceName("printer._tcp."));

  // Too many labels
  EXPECT_FALSE(MdnsNames::IsValidServiceName("pretty.printer._tcp."));
}

// Tests |IsValidInstanceName|.
TEST(MdnsNamesTest, IsValidInstanceName) {
  EXPECT_TRUE(MdnsNames::IsValidInstanceName("x"));
  EXPECT_TRUE(MdnsNames::IsValidInstanceName("x-ray machine"));
  EXPECT_TRUE(MdnsNames::IsValidInstanceName(
      "012345678901234567890123456789012345678901234567890123456789012"));

  // Empty.
  EXPECT_FALSE(MdnsNames::IsValidInstanceName(""));

  // Just a dot.
  EXPECT_FALSE(MdnsNames::IsValidInstanceName("."));

  // More than one label.
  EXPECT_FALSE(MdnsNames::IsValidInstanceName("gopher.cow"));

  // Too long.
  EXPECT_FALSE(MdnsNames::IsValidInstanceName(
      "0123456789012345678901234567890123456789012345678901234567890123"));
}

// Tests |IsValidSubtypeName|.
TEST(MdnsNamesTest, IsValidSubtypeName) {
  EXPECT_TRUE(MdnsNames::IsValidSubtypeName("x"));
  EXPECT_TRUE(MdnsNames::IsValidSubtypeName("x-ray machine"));
  EXPECT_TRUE(MdnsNames::IsValidSubtypeName(
      "012345678901234567890123456789012345678901234567890123456789012"));

  // Empty.
  EXPECT_FALSE(MdnsNames::IsValidSubtypeName(""));

  // Just a dot.
  EXPECT_FALSE(MdnsNames::IsValidSubtypeName("."));

  // More than one label.
  EXPECT_FALSE(MdnsNames::IsValidSubtypeName("gopher.cow"));

  // Too long.
  EXPECT_FALSE(MdnsNames::IsValidSubtypeName(
      "0123456789012345678901234567890123456789012345678901234567890123"));
}

// Tests |IsValidTextString|.
TEST(MdnsNamesTest, IsValidTextString) {
  EXPECT_TRUE(MdnsNames::IsValidTextString(""));
  EXPECT_TRUE(MdnsNames::IsValidTextString("."));
  EXPECT_TRUE(MdnsNames::IsValidTextString("x.y"));
  EXPECT_TRUE(MdnsNames::IsValidTextString("x=y"));
  EXPECT_TRUE(MdnsNames::IsValidTextString("x"));
  EXPECT_TRUE(MdnsNames::IsValidTextString("x-ray machine"));
  EXPECT_TRUE(MdnsNames::IsValidTextString(
      "012345678901234567890123456789012345678901234567890123456789012345678901"
      "234567890123456789012345678901234567890123456789012345678901234567890123"
      "456789012345678901234567890123456789012345678901234567890123456789012345"
      "678901234567890123456789012345678901234"));

  // Too long.
  EXPECT_FALSE(MdnsNames::IsValidTextString(
      "012345678901234567890123456789012345678901234567890123456789012345678901"
      "234567890123456789012345678901234567890123456789012345678901234567890123"
      "456789012345678901234567890123456789012345678901234567890123456789012345"
      "6789012345678901234567890123456789012345"));
}

// Tests |AltHostName|.
TEST(MdnsNamesTest, AltHostName) {
  EXPECT_EQ("123456789ABC", MdnsNames::AltHostName("fuchsia-1234-5678-9abc"));
  EXPECT_EQ("ABCDEFABCDEF", MdnsNames::AltHostName("fuchsia-abcd-efab-cdef"));
  EXPECT_EQ("000000000000", MdnsNames::AltHostName("fuchsia-0000-0000-0000"));
  EXPECT_EQ("unexpected format", MdnsNames::AltHostName("unexpected format"));
  EXPECT_EQ("longer unexpected format", MdnsNames::AltHostName("longer unexpected format"));
}

}  // namespace test
}  // namespace mdns
