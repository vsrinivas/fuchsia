// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/redact/redactor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/utils/redact/redactor.h"

namespace forensics {
namespace {

class IdentityRedactorTest : public ::testing::Test {
 protected:
  std::string Redact(std::string text) { return redactor_.Redact(text); }

 private:
  IdentityRedactor redactor_;
};

TEST_F(IdentityRedactorTest, Check) {
  EXPECT_EQ(Redact("Email: alice@website.tld"), "Email: alice@website.tld");
}

class RedactorTest : public ::testing::Test {
 protected:
  std::string Redact(std::string text) { return redactor_.Redact(text); }

  const Redactor& redactor() const { return redactor_; }

 private:
  Redactor redactor_;
};

TEST_F(RedactorTest, Check) {
  EXPECT_EQ(Redact("Email: alice@website.tld"), "Email: <REDACTED-EMAIL>");
  EXPECT_EQ(Redact("IPv4: 8.8.8.8"), "IPv4: <REDACTED-IPV4: 1>");
  EXPECT_EQ(Redact("IPv46: ::ffff:12.34.56.78"), "IPv46: ::ffff:<REDACTED-IPV4: 2>");
  EXPECT_EQ(Redact("IPv46h: ::ffff:ab12:34cd"), "IPv46h: ::ffff:<REDACTED-IPV4: 3>");
  EXPECT_EQ(Redact("not_IPv46h: ::ffff:ab12:34cd:5"), "not_IPv46h: <REDACTED-IPV6: 4>");
  EXPECT_EQ(Redact("IPv6: 2001:503:eEa3:0:0:0:0:30"), "IPv6: <REDACTED-IPV6: 5>");
  EXPECT_EQ(Redact("IPv6C: [::/0 via 2082::7d84:c1dc:ab34:656a nic 4]"),
            "IPv6C: [::/0 via <REDACTED-IPV6: 6> nic 4]");
  EXPECT_EQ(Redact("IPv6LL: fe80::7d84:c1dc:ab34:656a"), "IPv6LL: fe80:<REDACTED-IPV6-LL: 7>");
  EXPECT_EQ(Redact("UUID: ddd0fA34-1016-11eb-adc1-0242ac120002"), "UUID: <REDACTED-UUID>");
  EXPECT_EQ(Redact("MAC address: 00:0a:95:9F:68:16 12:34:95:9F:68:16"),
            "MAC address: 00:0a:95:<REDACTED-MAC: 8> 12:34:95:<REDACTED-MAC: 9>");
  EXPECT_EQ(Redact("SSID: <ssid-666F6F> <ssid-77696669>"),
            "SSID: <REDACTED-SSID: 10> <REDACTED-SSID: 11>");
  EXPECT_EQ(Redact("HTTP: http://fuchsia.dev/"), "HTTP: <REDACTED-URL>");
  EXPECT_EQ(Redact("HTTPS: https://fuchsia.dev/"), "HTTPS: <REDACTED-URL>");
  EXPECT_EQ(Redact("Combined: Email alice@website.tld, IPv4 8.8.8.8"),
            "Combined: Email <REDACTED-EMAIL>, IPv4 <REDACTED-IPV4: 1>");
  EXPECT_EQ(Redact("service::fidl service:fidl"), "service::fidl service:fidl");
  EXPECT_EQ(Redact("456 1234567890abcdefABCDEF0123456789 1.2.3.4"),
            "456 <REDACTED-HEX: 13> <REDACTED-IPV4: 12>");
  EXPECT_EQ(Redact("current: 0.8.8.8"), "current: 0.8.8.8");
  EXPECT_EQ(Redact("loopback: 127.8.8.8"), "loopback: 127.8.8.8");
  EXPECT_EQ(Redact("link_local: 169.254.8.8"), "link_local: 169.254.8.8");
  EXPECT_EQ(Redact("link_local_multicast: 224.0.0.8"), "link_local_multicast: 224.0.0.8");
  EXPECT_EQ(Redact("broadcast: 255.255.255.255"), "broadcast: 255.255.255.255");
  EXPECT_EQ(Redact("not_broadcast: 255.255.255.254"), "not_broadcast: <REDACTED-IPV4: 14>");
  EXPECT_EQ(Redact("not_link_local_multicast: 224.0.1.8"),
            "not_link_local_multicast: <REDACTED-IPV4: 15>");
  EXPECT_EQ(Redact("local_multicast_1: fF41::1234:5678:9aBc"),
            "local_multicast_1: fF41::1234:5678:9aBc");
  EXPECT_EQ(Redact("local_multicast_2: Ffe2:1:2:33:abcd:ef0:6789:456"),
            "local_multicast_2: Ffe2:1:2:33:abcd:ef0:6789:456");
  EXPECT_EQ(Redact("multicast: fF43:abcd::ef0:6789:456"),
            "multicast: fF43:<REDACTED-IPV6-MULTI: 16>");
  EXPECT_EQ(Redact("link_local_8: fe89:123::4567:8:90"),
            "link_local_8: fe89:<REDACTED-IPV6-LL: 17>");
  EXPECT_EQ(Redact("link_local_b: FEB2:123::4567:8:90"),
            "link_local_b: FEB2:<REDACTED-IPV6-LL: 18>");
  EXPECT_EQ(Redact("not_link_local: fec1:123::4567:8:90"), "not_link_local: <REDACTED-IPV6: 19>");
  EXPECT_EQ(Redact("not_link_local_2: fe71:123::4567:8:90"),
            "not_link_local_2: <REDACTED-IPV6: 20>");
  EXPECT_EQ(Redact("not_address_1: 12:34::"), "not_address_1: 12:34::");
  EXPECT_EQ(Redact("not_address_2: ::12:34"), "not_address_2: ::12:34");
  EXPECT_EQ(Redact("v6_colons_3_fields: ::12:34:5"), "v6_colons_3_fields: <REDACTED-IPV6: 21>");
  EXPECT_EQ(Redact("v6_3_fields_colons: 12:34:5::"), "v6_3_fields_colons: <REDACTED-IPV6: 22>");
  EXPECT_EQ(Redact("v6_colons_7_fields: ::12:234:35:46:5:6:7"),
            "v6_colons_7_fields: <REDACTED-IPV6: 23>");
  EXPECT_EQ(Redact("v6_7_fields_colons: 12:234:35:46:5:6:7::"),
            "v6_7_fields_colons: <REDACTED-IPV6: 24>");
  EXPECT_EQ(Redact("v6_colons_8_fields: ::12:234:35:46:5:6:7:8"),
            "v6_colons_8_fields: <REDACTED-IPV6: 23>:8");
  EXPECT_EQ(Redact("v6_8_fields_colons: 12:234:35:46:5:6:7:8::"),
            "v6_8_fields_colons: <REDACTED-IPV6: 25>::");
  EXPECT_EQ(Redact("obfuscated_gaia_id: 106986199446298680449"),
            "obfuscated_gaia_id: <REDACTED-OBFUSCATED-GAIA-ID: 26>");
}

TEST_F(RedactorTest, Canary) {
  EXPECT_EQ(Redact(redactor().UnredactedCanary()), redactor().RedactedCanary());
}

}  // namespace
}  // namespace forensics
