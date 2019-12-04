// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/config.h"

#include <zircon/errors.h>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

constexpr CrashServerConfig::UploadPolicy kDisabled = CrashServerConfig::UploadPolicy::DISABLED;
constexpr CrashServerConfig::UploadPolicy kEnabled = CrashServerConfig::UploadPolicy::ENABLED;
constexpr CrashServerConfig::UploadPolicy kReadFromPrivacySettings =
    CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS;

void CheckEmptyConfig(const Config& config) {
  EXPECT_EQ(config.crash_server.upload_policy, kDisabled);
  EXPECT_EQ(config.crash_server.url, nullptr);
}

TEST(ConfigTest, ParseConfig_ValidConfig_UploadDisabled) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/valid_config_upload_disabled.json", &config), ZX_OK);
  EXPECT_EQ(config.crash_server.upload_policy, kDisabled);
  EXPECT_EQ(config.crash_server.url, nullptr);
}

TEST(ConfigTest, ParseConfig_ValidConfig_UploadEnabled) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/valid_config_upload_enabled.json", &config), ZX_OK);
  EXPECT_EQ(config.crash_server.upload_policy, kEnabled);
  EXPECT_EQ(*config.crash_server.url, "http://localhost:1234");
}

TEST(ConfigTest, ParseConfig_ValidConfig_UploadReadFromPrivacySettings) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/valid_config_upload_read_from_privacy_settings.json", &config),
            ZX_OK);
  EXPECT_EQ(config.crash_server.upload_policy, kReadFromPrivacySettings);
  EXPECT_EQ(*config.crash_server.url, "http://localhost:1234");
}

TEST(ConfigTest, ParseConfig_ValidConfig_UploadDisabledServerUrlIgnored) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/valid_config_upload_disabled_spurious_server.json", &config),
            ZX_OK);
  EXPECT_EQ(config.crash_server.upload_policy, kDisabled);
  // Even though a URL is set in the config file, we check that it is not set in the struct.
  EXPECT_EQ(config.crash_server.url, nullptr);
}

TEST(ConfigTest, ParseConfig_MissingConfig) {
  Config config;
  ASSERT_EQ(ParseConfig("undefined file", &config), ZX_ERR_IO);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_SpuriousField) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/bad_schema_spurious_field_config.json", &config),
            ZX_ERR_INTERNAL);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_MissingRequiredField) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/bad_schema_missing_required_field_config.json", &config),
            ZX_ERR_INTERNAL);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_MissingServerUrlWithUploadEnabled) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/bad_schema_missing_server_upload_enabled_config.json", &config),
            ZX_ERR_INTERNAL);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_MissingServerUrlWithUploadReadFromPrivacySettings) {
  Config config;
  ASSERT_EQ(ParseConfig(
                "/pkg/data/bad_schema_missing_server_upload_read_from_privacy_settings_config.json",
                &config),
            ZX_ERR_INTERNAL);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_InvalidUploadPolicy) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/bad_schema_invalid_upload_policy_config.json", &config),
            ZX_ERR_INTERNAL);
  CheckEmptyConfig(config);
}

}  // namespace

// Pretty-prints CrashServerConfig::UploadPolicy in gTest matchers instead of the default byte
// string in case of failed expectations.
void PrintTo(const CrashServerConfig::UploadPolicy& upload_policy, std::ostream* os) {
  *os << ToString(upload_policy);
}

}  // namespace feedback
