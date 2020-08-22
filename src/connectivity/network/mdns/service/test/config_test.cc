// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/config.h"

#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace mdns {
namespace test {

const char kTestDir[] = "/tmp/mdns_config_test";
const char kHostName[] = "test-host-name";

bool WriteFile(const std::string& file, const std::string& to_write) {
  return files::WriteFile(std::string(kTestDir) + std::string("/") + file, to_write.c_str(),
                          to_write.length());
}

bool operator==(const std::unique_ptr<Mdns::Publication>& lhs,
                const std::unique_ptr<Mdns::Publication>& rhs) {
  return (lhs == nullptr && rhs == nullptr) ||
         (lhs != nullptr && rhs != nullptr && lhs->port_ == rhs->port_ &&
          lhs->text_ == rhs->text_ && lhs->ptr_ttl_seconds_ == rhs->ptr_ttl_seconds_ &&
          lhs->srv_ttl_seconds_ == rhs->srv_ttl_seconds_ &&
          lhs->txt_ttl_seconds_ == rhs->txt_ttl_seconds_);
}

bool operator==(const Config::Publication& lhs, const Config::Publication& rhs) {
  return lhs.service_ == rhs.service_ && lhs.instance_ == rhs.instance_ &&
         lhs.publication_ == rhs.publication_ && lhs.perform_probe_ == rhs.perform_probe_ &&
         lhs.media_ == rhs.media_;
}

static const inet::IpPort kDefaultPort = inet::IpPort::From_uint16_t(5353);
static const inet::SocketAddress kDefaultV4MulticastAddress(224, 0, 0, 251, kDefaultPort);
static const inet::SocketAddress kDefaultV6MulticastAddress(0xff02, 0xfb, kDefaultPort);
static const inet::SocketAddress kDefaultV4BindAddress(INADDR_ANY, kDefaultPort);
static const inet::SocketAddress kDefaultV6BindAddress(in6addr_any, kDefaultPort);
static const ReplyAddress kDefaulMulticastReplyAddress(kDefaultV4MulticastAddress,
                                                       inet::IpAddress(), Media::kBoth);

// Tests behavior when there is no config directory.
TEST(ConfigTest, NoDir) {
  Config under_test;
  under_test.ReadConfigFiles(kHostName, kTestDir);
  EXPECT_TRUE(under_test.valid());
  EXPECT_EQ("", under_test.error());
  EXPECT_TRUE(under_test.perform_host_name_probe());
  EXPECT_TRUE(under_test.publications().empty());
  EXPECT_EQ(kDefaultPort, under_test.addresses().port());
  EXPECT_EQ(kDefaultV4MulticastAddress, under_test.addresses().v4_multicast());
  EXPECT_EQ(kDefaultV6MulticastAddress, under_test.addresses().v6_multicast());
  EXPECT_EQ(kDefaultV4BindAddress, under_test.addresses().v4_bind());
  EXPECT_EQ(kDefaultV6BindAddress, under_test.addresses().v6_bind());
  EXPECT_EQ(kDefaulMulticastReplyAddress, under_test.addresses().multicast_reply());

  EXPECT_TRUE(files::DeletePath(kTestDir, true));
}

// Tests behavior when there are no config files.
TEST(ConfigTest, Empty) {
  EXPECT_TRUE(files::CreateDirectory(kTestDir));

  Config under_test;
  under_test.ReadConfigFiles(kHostName, kTestDir);
  EXPECT_TRUE(under_test.valid());
  EXPECT_EQ("", under_test.error());
  EXPECT_TRUE(under_test.perform_host_name_probe());
  EXPECT_TRUE(under_test.publications().empty());
  EXPECT_EQ(kDefaultPort, under_test.addresses().port());
  EXPECT_EQ(kDefaultV4MulticastAddress, under_test.addresses().v4_multicast());
  EXPECT_EQ(kDefaultV6MulticastAddress, under_test.addresses().v6_multicast());
  EXPECT_EQ(kDefaultV4BindAddress, under_test.addresses().v4_bind());
  EXPECT_EQ(kDefaultV6BindAddress, under_test.addresses().v6_bind());
  EXPECT_EQ(kDefaulMulticastReplyAddress, under_test.addresses().multicast_reply());

  EXPECT_TRUE(files::DeletePath(kTestDir, true));
}

// Tests behavior when there is one valid config file.
TEST(ConfigTest, OneValidFile) {
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("valid", R"({
    "port": 5454,
    "v4_multicast_address": "225.1.1.252",
    "v6_multicast_address": "ff03::fc",
    "perform_host_name_probe": false,
    "publications" : [
      {"service" : "_fuchsia._udp.", "port" : 5353, "perform_probe" : false,
       "text": ["chins=2", "thumbs=10"], "media": "wireless"}
    ]
  })"));

  inet::IpPort expected_port = inet::IpPort::From_uint16_t(5454);
  inet::SocketAddress expected_v4_multicast_address(225, 1, 1, 252, expected_port);
  inet::SocketAddress expected_v6_multicast_address(0xff03, 0xfc, expected_port);
  inet::SocketAddress expected_v4_bind_address(INADDR_ANY, expected_port);
  inet::SocketAddress expected_v6_bind_address(in6addr_any, expected_port);
  ReplyAddress expected_multicast_reply_address(expected_v4_multicast_address, inet::IpAddress(),
                                                Media::kBoth);

  Config under_test;
  under_test.ReadConfigFiles(kHostName, kTestDir);
  EXPECT_TRUE(under_test.valid());
  EXPECT_EQ("", under_test.error());
  EXPECT_FALSE(under_test.perform_host_name_probe());
  EXPECT_EQ(1u, under_test.publications().size());
  if (!under_test.publications().empty()) {
    EXPECT_TRUE(
        (Config::Publication{
            .service_ = "_fuchsia._udp.",
            .instance_ = kHostName,
            .publication_ = std::make_unique<Mdns::Publication>(Mdns::Publication{
                .port_ = inet::IpPort::From_uint16_t(5353), .text_ = {"chins=2", "thumbs=10"}}),
            .perform_probe_ = false,
            .media_ = Media::kWireless}) == under_test.publications()[0]);
  }

  EXPECT_EQ(expected_port, under_test.addresses().port());
  EXPECT_EQ(expected_v4_multicast_address, under_test.addresses().v4_multicast());
  EXPECT_EQ(expected_v6_multicast_address, under_test.addresses().v6_multicast());
  EXPECT_EQ(expected_v4_bind_address, under_test.addresses().v4_bind());
  EXPECT_EQ(expected_v6_bind_address, under_test.addresses().v6_bind());
  EXPECT_EQ(expected_multicast_reply_address, under_test.addresses().multicast_reply());

  EXPECT_TRUE(files::DeletePath(kTestDir, true));
}

// Tests behavior when there is one valid config file.
TEST(ConfigTest, OneInvalidFile) {
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("invalid", R"({
    "dwarves": 7
  })"));

  Config under_test;
  under_test.ReadConfigFiles(kHostName, kTestDir);
  EXPECT_FALSE(under_test.valid());
  EXPECT_NE("", under_test.error());

  EXPECT_TRUE(files::DeletePath(kTestDir, true));
}

// Tests behavior when there is one valid and one invalid config file.
TEST(ConfigTest, OneValidOneInvalidFile) {
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("valid", R"({
    "perform_host_name_probe": false,
    "publications" : [
      {"service" : "_fuchsia._udp.", "port" : 5353, "perform_probe" : false}
    ]
  })"));
  EXPECT_TRUE(WriteFile("invalid", R"({
    "dwarves": 7
  })"));

  Config under_test;
  under_test.ReadConfigFiles(kHostName, kTestDir);
  EXPECT_FALSE(under_test.valid());
  EXPECT_NE("", under_test.error());

  EXPECT_TRUE(files::DeletePath(kTestDir, true));
}

// Tests behavior when there are two valid config files.
TEST(ConfigTest, TwoValidFiles) {
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("valid1", R"({
    "perform_host_name_probe": false,
    "publications" : [
      {"service" : "_fuchsia._udp.", "port" : 5353, "perform_probe" : false, "media": "wired"}
    ]
  })"));
  EXPECT_TRUE(WriteFile("valid2", R"({
    "publications" : [
      {"service" : "_footstool._udp.", "instance": "puffy", "port" : 1234, "media": "both"}
    ]
  })"));

  Config under_test;
  under_test.ReadConfigFiles(kHostName, kTestDir);
  EXPECT_TRUE(under_test.valid());
  EXPECT_EQ("", under_test.error());
  EXPECT_EQ(inet::IpPort::From_uint16_t(5353), under_test.addresses().port());
  EXPECT_FALSE(under_test.perform_host_name_probe());
  EXPECT_EQ(2u, under_test.publications().size());

  size_t fuchsia_index = (under_test.publications()[0].service_ == "_fuchsia._udp.") ? 0 : 1;

  EXPECT_TRUE(
      (Config::Publication{.service_ = "_fuchsia._udp.",
                           .instance_ = kHostName,
                           .publication_ = std::make_unique<Mdns::Publication>(
                               Mdns::Publication{.port_ = inet::IpPort::From_uint16_t(5353)}),
                           .perform_probe_ = false,
                           .media_ = Media::kWired}) == under_test.publications()[fuchsia_index]);
  EXPECT_TRUE((Config::Publication{
                  .service_ = "_footstool._udp.",
                  .instance_ = "puffy",
                  .publication_ = std::make_unique<Mdns::Publication>(
                      Mdns::Publication{.port_ = inet::IpPort::From_uint16_t(1234)}),
                  .perform_probe_ = true,
                  .media_ = Media::kBoth}) == under_test.publications()[1 - fuchsia_index]);

  EXPECT_TRUE(files::DeletePath(kTestDir, true));
}

// Tests behavior when there are two valid config files that conflict.
TEST(ConfigTest, TwoConflictingValidFiles) {
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("valid1", R"({
    "perform_host_name_probe": false,
    "publications" : [
      {"service" : "_fuchsia._udp.", "port" : 5353, "perform_probe" : false}
    ]
  })"));
  EXPECT_TRUE(WriteFile("valid2", R"({
    "perform_host_name_probe": true,
    "publications" : [
      {"service" : "_footstool._udp.", "instance": "puffy", "port" : 1234}
    ]
  })"));

  Config under_test;
  under_test.ReadConfigFiles(kHostName, kTestDir);
  EXPECT_FALSE(under_test.valid());
  EXPECT_NE("", under_test.error());

  EXPECT_TRUE(files::DeletePath(kTestDir, true));
}

}  // namespace test
}  // namespace mdns
