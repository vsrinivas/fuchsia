// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/config.h"

#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::feedback {
namespace {

constexpr auto kUploadDisabled = crash_reports::Config::UploadPolicy::kDisabled;
constexpr auto kUploadEnabled = crash_reports::Config::UploadPolicy::kEnabled;

class ConfigTest : public testing::Test {
 protected:
  // Writes |config| to a file and returns the path of the config.
  std::string WriteConfig(const std::string& config) {
    std::string path;
    FX_CHECK(temp_dir_.NewTempFileWithData(config, &path));
    return path;
  }

  std::optional<BuildTypeConfig> ParseConfig(const std::string& config) {
    return GetBuildTypeConfig(WriteConfig(config));
  }

 private:
  files::ScopedTempDir temp_dir_;
};

TEST_F(ConfigTest, MissingEnableDataRedaction) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, MissingEnableHourlySnapshots) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, MissingEnableLimitInspectData) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, SpuriousField) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false,
  "spurious": ""
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, EnableDataRedactionTrue) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": true,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_data_redaction);
}

TEST_F(ConfigTest, EnableDataRedactionFalse) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->enable_data_redaction);
}

TEST_F(ConfigTest, EnableDataRedactionNotBoolean) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": "",
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, EnableHourlySnapshotsTrue) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_hourly_snapshots": true,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_hourly_snapshots);
}

TEST_F(ConfigTest, EnableHourlySnapshotsFalse) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->enable_hourly_snapshots);
}

TEST_F(ConfigTest, EnableHourlySnapshotsNotBoolean) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_hourly_snapshots": "",
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, EnableLimitInspectDataTrue) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": true
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_limit_inspect_data);
}

TEST_F(ConfigTest, EnableLimitInspectDataFalse) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->enable_limit_inspect_data);
}

TEST_F(ConfigTest, EnableLimitInspectDataNotBoolean) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": ""
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, UseOverrideConfig) {
  const std::string override_path = WriteConfig(R"({
  "enable_data_redaction": true,
  "enable_hourly_snapshots": true,
  "enable_limit_inspect_data": true
})");

  const std::optional<BuildTypeConfig> config = GetBuildTypeConfig(override_path, "/bad/path");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_data_redaction);
  EXPECT_TRUE(config->enable_hourly_snapshots);
  EXPECT_TRUE(config->enable_limit_inspect_data);
}

TEST_F(ConfigTest, UseDefaultConfig) {
  const std::string default_path = WriteConfig(R"({
  "enable_data_redaction": true,
  "enable_hourly_snapshots": true,
  "enable_limit_inspect_data": true
})");

  const std::optional<BuildTypeConfig> config = GetBuildTypeConfig("/bad/path", default_path);

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_data_redaction);
  EXPECT_TRUE(config->enable_hourly_snapshots);
  EXPECT_TRUE(config->enable_limit_inspect_data);
}

TEST_F(ConfigTest, MissingOverrideAndDefaultConfigs) {
  const std::optional<BuildTypeConfig> config = GetBuildTypeConfig("/bad/path", "/bad/path");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, GetCrashReportsConfig) {
  const std::string default_config_path = WriteConfig(R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "disabled"
})");

  const std::string override_config_path = WriteConfig(R"({
    "daily_per_product_quota": 100,
    "crash_report_upload_policy": "enabled"
})");

  const std::string invalid_config_path = WriteConfig(R"({
    "invalid": {}
})");

  // The override config should be read regardless of the default config being valid.
  auto config = GetCrashReportsConfig("/bad/path", override_config_path);
  ASSERT_TRUE(config);
  EXPECT_EQ(config->crash_report_upload_policy, kUploadEnabled);
  EXPECT_EQ(config->daily_per_product_quota, 100u);

  config = GetCrashReportsConfig(invalid_config_path, override_config_path);
  ASSERT_TRUE(config);
  EXPECT_EQ(config->crash_report_upload_policy, kUploadEnabled);
  EXPECT_EQ(config->daily_per_product_quota, 100u);

  // The default config should be read if there's an issue using the override config.
  config = GetCrashReportsConfig(default_config_path, "/bad/path");
  ASSERT_TRUE(config);
  EXPECT_EQ(config->crash_report_upload_policy, kUploadDisabled);
  EXPECT_EQ(config->daily_per_product_quota, std::nullopt);

  config = GetCrashReportsConfig(default_config_path, invalid_config_path);
  ASSERT_TRUE(config);
  EXPECT_EQ(config->crash_report_upload_policy, kUploadDisabled);
  EXPECT_EQ(config->daily_per_product_quota, std::nullopt);

  // No config should be returned if neither config can be read.
  EXPECT_FALSE(GetCrashReportsConfig("/bad/path", "/bad/path"));
  EXPECT_FALSE(GetCrashReportsConfig(invalid_config_path, invalid_config_path));
}

TEST_F(ConfigTest, GetFeedbackDataConfig) {
  const std::string config_path = WriteConfig(R"({
    "annotation_allowlist": [
      "annotation_one",
      "annotation_two"
    ],
    "attachment_allowlist": [
      "attachment_one"
    ]
})");

  EXPECT_FALSE(GetFeedbackDataConfig("/bad/path"));

  const auto config = GetFeedbackDataConfig(config_path);
  ASSERT_TRUE(config);
  EXPECT_EQ(config->annotation_allowlist, std::set<std::string>({
                                              "annotation_one",
                                              "annotation_two",
                                          }));
  EXPECT_EQ(config->attachment_allowlist, std::set<std::string>({
                                              "attachment_one",
                                          }));
}

}  // namespace
}  // namespace forensics::feedback
