// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/config.h"

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/common/type_converters.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace mdns {
namespace test {

const char kTestDir[] = "/tmp/mdns_config_test";
const char kBootTestDir[] = "/tmp/mdns_config_boot_test";
const char kHostName[] = "test-host-name";

bool WriteFile(const std::string& file, const std::string& to_write,
               const std::string& directory = kTestDir) {
  return files::WriteFile(std::string(directory) + std::string("/") + file, to_write.c_str(),
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

// Tests behavior when there is no config directory.
TEST(ConfigTest, NoDir) {
  Config under_test;
  under_test.ReadConfigFiles(kHostName, kTestDir);
  EXPECT_TRUE(under_test.valid());
  EXPECT_EQ("", under_test.error());
  EXPECT_TRUE(under_test.perform_host_name_probe());
  EXPECT_TRUE(under_test.publications().empty());

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

  EXPECT_TRUE(files::DeletePath(kTestDir, true));
}

// Tests behavior when there is one valid config file.
TEST(ConfigTest, OneValidFile) {
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("valid", R"({
    "perform_host_name_probe": false,
    "publications" : [
      {"service" : "_fuchsia._udp.", "port" : 5353, "perform_probe" : false,
       "text": ["chins=2", "thumbs=10"], "media": "wireless"}
    ],
    "alt_services": [ "_altsvc1._udp.", "_altsvc2._tcp." ]
  })"));

  Config under_test;
  under_test.ReadConfigFiles(kHostName, kTestDir);
  EXPECT_TRUE(under_test.valid());
  EXPECT_EQ("", under_test.error());
  EXPECT_FALSE(under_test.perform_host_name_probe());
  EXPECT_EQ(1u, under_test.publications().size());
  if (!under_test.publications().empty()) {
    EXPECT_TRUE((Config::Publication{
                    .service_ = "_fuchsia._udp.",
                    .instance_ = kHostName,
                    .publication_ = std::make_unique<Mdns::Publication>(Mdns::Publication{
                        .port_ = inet::IpPort::From_uint16_t(5353),
                        .text_ = {fidl::To<std::vector<uint8_t>>(std::string("chins=2")),
                                  fidl::To<std::vector<uint8_t>>(std::string("thumbs=10"))}}),
                    .perform_probe_ = false,
                    .media_ = Media::kWireless}) == under_test.publications()[0]);
  }
  EXPECT_EQ(2u, under_test.alt_services().size());
  EXPECT_EQ("_altsvc1._udp.", under_test.alt_services()[0]);
  EXPECT_EQ("_altsvc2._tcp.", under_test.alt_services()[1]);

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

// Test with a boot config directory with no config files
TEST(ConfigTest, EmptyBootConfigDir) {
  EXPECT_TRUE(files::CreateDirectory(kBootTestDir));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("valid1", R"({
    "perform_host_name_probe": false,
    "publications" : [
      {"service" : "_fuchsia._udp.", "port" : 5353, "perform_probe" : false}
    ]
  })"));

  Config under_test;
  under_test.ReadConfigFiles(kHostName, kBootTestDir, kTestDir);
  EXPECT_TRUE(under_test.valid());
  EXPECT_EQ("", under_test.error());

  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::DeletePath(kBootTestDir, true));
}

// Test with a boot config directory with overriding services.
TEST(ConfigTest, OverrideBootConfigDir) {
  EXPECT_TRUE(files::CreateDirectory(kBootTestDir));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("valid1", R"({
    "perform_host_name_probe": false,
    "publications" : [
      {"service" : "_fuchsia._udp.", "port" : 5353, "perform_probe" : false}
    ]
  })",
                        kTestDir));
  EXPECT_TRUE(WriteFile("valid1", R"({
    "perform_host_name_probe": false,
    "publications" : [
      {"service" : "_fuchsia._udp.", "port" : 12345, "perform_probe" : false,
      "media" : "wired",
      "text" : [ "one", "two"]}
    ]
  })",
                        kBootTestDir));

  Config under_test;
  under_test.ReadConfigFiles(kHostName, kBootTestDir, kTestDir);
  EXPECT_TRUE(under_test.valid());
  EXPECT_EQ("", under_test.error());

  // Config from boot dir should always be first.
  EXPECT_TRUE((Config::Publication{.service_ = "_fuchsia._udp.",
                                   .instance_ = kHostName,
                                   .publication_ = Mdns::Publication::Create(
                                       inet::IpPort::From_uint16_t(12345),
                                       {fidl::To<std::vector<uint8_t>>(std::string("one")),
                                        fidl::To<std::vector<uint8_t>>(std::string("two"))}),
                                   .perform_probe_ = false,
                                   .media_ = Media::kWired}) == under_test.publications()[0]);
  EXPECT_TRUE(
      (Config::Publication{.service_ = "_fuchsia._udp.",
                           .instance_ = kHostName,
                           .publication_ = std::make_unique<Mdns::Publication>(
                               Mdns::Publication{.port_ = inet::IpPort::From_uint16_t(5353)}),
                           .perform_probe_ = false,
                           .media_ = Media::kBoth}) == under_test.publications()[1]);

  Config under_test_reversed;
  under_test_reversed.ReadConfigFiles(kHostName, kTestDir, kBootTestDir);
  EXPECT_TRUE(under_test_reversed.valid());
  EXPECT_EQ("", under_test_reversed.error());

  // Config from boot dir should always be first, in this case the parameters are reversed, so the
  // order should be too.
  EXPECT_TRUE((Config::Publication{.service_ = "_fuchsia._udp.",
                                   .instance_ = kHostName,
                                   .publication_ = Mdns::Publication::Create(
                                       inet::IpPort::From_uint16_t(12345),
                                       {fidl::To<std::vector<uint8_t>>(std::string("one")),
                                        fidl::To<std::vector<uint8_t>>(std::string("two"))}),
                                   .perform_probe_ = false,
                                   .media_ = Media::kWired}) ==
              under_test_reversed.publications()[1]);
  EXPECT_TRUE(
      (Config::Publication{.service_ = "_fuchsia._udp.",
                           .instance_ = kHostName,
                           .publication_ = std::make_unique<Mdns::Publication>(
                               Mdns::Publication{.port_ = inet::IpPort::From_uint16_t(5353)}),
                           .perform_probe_ = false,
                           .media_ = Media::kBoth}) == under_test_reversed.publications()[0]);

  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::DeletePath(kBootTestDir, true));
}

}  // namespace test
}  // namespace mdns
