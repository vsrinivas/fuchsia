// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/config.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace crash_reports {
namespace {

constexpr Config::UploadPolicy kDisabled = Config::UploadPolicy::kDisabled;
constexpr Config::UploadPolicy kEnabled = Config::UploadPolicy::kEnabled;
constexpr Config::UploadPolicy kReadFromPrivacySettings =
    Config::UploadPolicy::kReadFromPrivacySettings;

class ConfigTest : public testing::Test {
 protected:
  std::string WriteConfig(const std::string& config) {
    std::string path;
    FX_CHECK(tmp_dir_.NewTempFileWithData(config, &path));
    return path;
  }

 private:
  files::ScopedTempDir tmp_dir_;
};

// Parse |config_str| into |var| or assert if the operation fails.
#define PARSE_OR_ASSERT(var, config_str)                 \
  auto tmp_##var = ParseConfig(WriteConfig(config_str)); \
  ASSERT_TRUE(tmp_##var.has_value());                    \
  auto var = std::move(tmp_##var.value());

// Parse |config_str| and assert it is malformed.
#define ASSERT_IS_BAD_CONFIG(config_str) \
  ASSERT_FALSE(ParseConfig(WriteConfig(config_str)).has_value());

TEST_F(ConfigTest, MissingDailyPerProductQuota) {
  ASSERT_IS_BAD_CONFIG(R"({
    "crash_report_upload_policy": "disabled",
    "hourly_snapshot": false
})");
}

TEST_F(ConfigTest, BadDailyPerProductQuotaValue) {
  ASSERT_IS_BAD_CONFIG(R"({
    "daily_per_product_quota": "",
    "crash_report_upload_policy": "disabled",
    "hourly_snapshot": false
})");
}

TEST_F(ConfigTest, MissingCrashReportUploadPolicy) {
  ASSERT_IS_BAD_CONFIG(R"({
    "daily_per_product_quota": -1,
    "hourly_snapshot": false
})");
}

TEST_F(ConfigTest, BadCrashReportUploadPolicy) {
  ASSERT_IS_BAD_CONFIG(R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "other",
    "hourly_snapshot": false
})");
}

TEST_F(ConfigTest, BadHourlySnapshotField) {
  ASSERT_IS_BAD_CONFIG(R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "disabled",
    "hourly_snapshot": ""
})");
}

TEST_F(ConfigTest, MissingHourlySnapshot) {
  ASSERT_IS_BAD_CONFIG(R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "disabled"
})");
}

TEST_F(ConfigTest, SpruiousFields) {
  ASSERT_IS_BAD_CONFIG(R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "disabled",
    "hourly_snapshot": false,
    "spurious": ""
})");
}

TEST_F(ConfigTest, UploadDisabled) {
  PARSE_OR_ASSERT(config, R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "disabled",
    "hourly_snapshot": false
})");
  EXPECT_EQ(config.crash_report_upload_policy, kDisabled);
}

TEST_F(ConfigTest, UploadEnabled) {
  PARSE_OR_ASSERT(config, R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "enabled",
    "hourly_snapshot": false
})");
  EXPECT_EQ(config.crash_report_upload_policy, kEnabled);
}

TEST_F(ConfigTest, UploadReadFromPrivacySettings) {
  PARSE_OR_ASSERT(config, R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "read_from_privacy_settings",
    "hourly_snapshot": false
})");
  EXPECT_EQ(config.crash_report_upload_policy, kReadFromPrivacySettings);
}

TEST_F(ConfigTest, PositiveDailyPerProductQuota) {
  PARSE_OR_ASSERT(config, R"({
    "daily_per_product_quota": 100,
    "crash_report_upload_policy": "enabled",
    "hourly_snapshot": false
})");
  ASSERT_TRUE(config.daily_per_product_quota.has_value());
  EXPECT_EQ(config.daily_per_product_quota.value(), 100u);
}

TEST_F(ConfigTest, ZeroDailyPerProductQuota) {
  PARSE_OR_ASSERT(config, R"({
    "daily_per_product_quota": 0,
    "crash_report_upload_policy": "enabled",
    "hourly_snapshot": false
})");
  ASSERT_FALSE(config.daily_per_product_quota.has_value());
}

TEST_F(ConfigTest, NegativeDailyPerProductQuota) {
  PARSE_OR_ASSERT(config, R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "enabled",
    "hourly_snapshot": false
})");
  ASSERT_FALSE(config.daily_per_product_quota.has_value());
}

TEST_F(ConfigTest, MissingConfig) { ASSERT_FALSE(ParseConfig("undefined file").has_value()); }

TEST_F(ConfigTest, HourlySnapshotTrue) {
  PARSE_OR_ASSERT(config, R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "enabled",
    "hourly_snapshot": true
   })");
  EXPECT_TRUE(config.hourly_snapshot);
}

TEST_F(ConfigTest, HourlySnapshotFalse) {
  PARSE_OR_ASSERT(config, R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "enabled",
    "hourly_snapshot": false
   })");
  EXPECT_FALSE(config.hourly_snapshot);
}

}  // namespace

// Pretty-prints Config::UploadPolicy in gTest matchers instead of the default byte
// string in case of failed expectations.
void PrintTo(const Config::UploadPolicy& upload_policy, std::ostream* os) {
  *os << ToString(upload_policy);
}

#undef ASSERT_IS_BAD_CONFIG
#undef PARSE_OR_ASSERT

}  // namespace crash_reports
}  // namespace forensics
