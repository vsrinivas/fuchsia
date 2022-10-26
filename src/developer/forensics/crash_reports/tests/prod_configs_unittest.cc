// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/lib/files/path.h"

namespace forensics::crash_reports {
namespace {

constexpr auto kDisabled = CrashServerConfig::UploadPolicy::DISABLED;
constexpr auto kEnabled = CrashServerConfig::UploadPolicy::ENABLED;
constexpr auto kReadFromPrivacySettings =
    CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS;

class ProdConfigTest : public testing::Test {
 public:
  static std::optional<Config> GetConfig(const std::string& config_filename) {
    return ParseConfig(files::JoinPath("/pkg/data/configs", config_filename));
  }
};

TEST_F(ProdConfigTest, Default) {
  const std::optional<Config> config = GetConfig("default.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_EQ(config->crash_server.upload_policy, kDisabled);
  EXPECT_EQ(config->daily_per_product_quota, std::nullopt);
  EXPECT_EQ(config->hourly_snapshot, false);
}

TEST_F(ProdConfigTest, UploadToProdServer) {
  const std::optional<Config> config = GetConfig("upload_to_prod_server.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_EQ(config->crash_server.upload_policy, kEnabled);
  EXPECT_EQ(config->daily_per_product_quota, std::nullopt);
  EXPECT_EQ(config->hourly_snapshot, false);
}

TEST_F(ProdConfigTest, User) {
  const std::optional<Config> config = GetConfig("user.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_EQ(config->crash_server.upload_policy, kReadFromPrivacySettings);
  EXPECT_EQ(config->daily_per_product_quota, 100);
  EXPECT_EQ(config->hourly_snapshot, false);
}

TEST_F(ProdConfigTest, Userdebug) {
  const std::optional<Config> config = GetConfig("userdebug.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_EQ(config->crash_server.upload_policy, kReadFromPrivacySettings);
  EXPECT_EQ(config->daily_per_product_quota, std::nullopt);
  EXPECT_EQ(config->hourly_snapshot, true);
}

}  // namespace
}  // namespace forensics::crash_reports
