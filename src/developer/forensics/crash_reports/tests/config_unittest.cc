// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/config.h"

#include <zircon/errors.h>

#include <gtest/gtest.h>

namespace forensics {
namespace crash_reports {
namespace {

#define UNWRAP_OR_ASSERT(var, expr)   \
  auto tmp_##var = expr;              \
  ASSERT_TRUE(tmp_##var.has_value()); \
  auto var = std::move(tmp_##var.value());

constexpr CrashServerConfig::UploadPolicy kDisabled = CrashServerConfig::UploadPolicy::DISABLED;
constexpr CrashServerConfig::UploadPolicy kEnabled = CrashServerConfig::UploadPolicy::ENABLED;
constexpr CrashServerConfig::UploadPolicy kReadFromPrivacySettings =
    CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS;

TEST(ConfigTest, ParseConfig_ValidConfig_UploadDisabled) {
  UNWRAP_OR_ASSERT(config, ParseConfig("/pkg/data/configs/valid_upload_disabled.json"));
  EXPECT_EQ(config.crash_server.upload_policy, kDisabled);
  EXPECT_EQ(config.crash_server.url, nullptr);
}

TEST(ConfigTest, ParseConfig_ValidConfig_UploadEnabled) {
  UNWRAP_OR_ASSERT(config, ParseConfig("/pkg/data/configs/valid_upload_enabled.json"));
  EXPECT_EQ(config.crash_server.upload_policy, kEnabled);
  EXPECT_EQ(*config.crash_server.url, "http://localhost:1234");
}

TEST(ConfigTest, ParseConfig_ValidConfig_UploadReadFromPrivacySettings) {
  UNWRAP_OR_ASSERT(config,
                   ParseConfig("/pkg/data/configs/valid_upload_read_from_privacy_settings.json"));
  EXPECT_EQ(config.crash_server.upload_policy, kReadFromPrivacySettings);
  EXPECT_EQ(*config.crash_server.url, "http://localhost:1234");
}

TEST(ConfigTest, ParseConfig_ValidConfig_UploadDisabledServerUrlIgnored) {
  UNWRAP_OR_ASSERT(config,
                   ParseConfig("/pkg/data/configs/valid_upload_disabled_spurious_server.json"));
  EXPECT_EQ(config.crash_server.upload_policy, kDisabled);
  // Even though a URL is set in the config file, we check that it is not set in the struct.
  EXPECT_EQ(config.crash_server.url, nullptr);
}

TEST(ConfigTest, ParseConfig_HasDailyPerProductQuota) {
  UNWRAP_OR_ASSERT(config, ParseConfig("/pkg/data/configs/has_quota.json"));
  ASSERT_TRUE(config.daily_per_product_quota.has_value());
  EXPECT_EQ(config.daily_per_product_quota.value(), 100u);
}

TEST(ConfigTest, ParseConfig_MissingDailyPerProductQuota) {
  UNWRAP_OR_ASSERT(config, ParseConfig("/pkg/data/configs/missing_quota.json"));
  EXPECT_FALSE(config.daily_per_product_quota.has_value());
}

TEST(ConfigTest, ParseConfig_MissingConfig) {
  ASSERT_FALSE(ParseConfig("undefined file").has_value());
}

TEST(ConfigTest, ParseConfig_BadConfig_SpuriousField) {
  ASSERT_FALSE(ParseConfig("/pkg/data/configs/bad_schema_spurious_field.json").has_value());
}

TEST(ConfigTest, ParseConfig_BadConfig_MissingRequiredField) {
  ASSERT_FALSE(ParseConfig("/pkg/data/configs/bad_schema_missing_required_field.json").has_value());
}

TEST(ConfigTest, ParseConfig_BadConfig_MissingServerUrlWithUploadEnabled) {
  ASSERT_FALSE(
      ParseConfig("/pkg/data/configs/bad_schema_missing_server_upload_enabled.json").has_value());
}

TEST(ConfigTest, ParseConfig_BadConfig_MissingServerUrlWithUploadReadFromPrivacySettings) {
  ASSERT_FALSE(
      ParseConfig(
          "/pkg/data/configs/bad_schema_missing_server_upload_read_from_privacy_settings.json")
          .has_value());
}

TEST(ConfigTest, ParseConfig_BadConfig_InvalidUploadPolicy) {
  ASSERT_FALSE(ParseConfig("/pkg/data/configs/bad_schema_invalid_upload_policy.json").has_value());
}

}  // namespace

// Pretty-prints CrashServerConfig::UploadPolicy in gTest matchers instead of the default byte
// string in case of failed expectations.
void PrintTo(const CrashServerConfig::UploadPolicy& upload_policy, std::ostream* os) {
  *os << ToString(upload_policy);
}

}  // namespace crash_reports
}  // namespace forensics
