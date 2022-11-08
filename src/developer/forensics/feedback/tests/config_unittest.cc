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

  std::optional<BoardConfig> ParseBoardConfig(const std::string& config) {
    return GetBoardConfig(WriteConfig(config));
  }

 private:
  files::ScopedTempDir temp_dir_;
};

TEST_F(ConfigTest, BoardConfigMissingPersistedLogsNumFiles) {
  const std::optional<BoardConfig> config = ParseBoardConfig(R"({
  "persisted_logs_total_size_kib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, BoardConfigMissingPersistedLogsTotalSizeKib) {
  const std::optional<BoardConfig> config = ParseBoardConfig(R"({
  "persisted_logs_num_files": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, BoardConfigSpuriousField) {
  const std::optional<BoardConfig> config = ParseBoardConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "spurious": ""
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, BoardConfigPersistedLogsNumFilesPositive) {
  const std::optional<BoardConfig> config = ParseBoardConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_num_files, 1u);
}

TEST_F(ConfigTest, BoardConfigPersistedLogsNumFilesZero) {
  const std::optional<BoardConfig> config = ParseBoardConfig(R"({
  "persisted_logs_num_files": 0,
  "persisted_logs_total_size_kib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, BoardConfigPersistedLogsNumFilesNegative) {
  const std::optional<BoardConfig> config = ParseBoardConfig(R"({
  "persisted_logs_num_files": -1,
  "persisted_logs_total_size_kib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, BoardConfigPersistedLogsNumFilesNotNumber) {
  const std::optional<BoardConfig> config = ParseBoardConfig(R"({
  "persisted_logs_num_files": "",
  "persisted_logs_total_size_kib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, BoardConfigPersistedLogsTotalSizeKibPositive) {
  const std::optional<BoardConfig> config = ParseBoardConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_total_size, StorageSize::Kilobytes(1));
}

TEST_F(ConfigTest, BoardConfigPersistedLogsTotalSizeKibZero) {
  const std::optional<BoardConfig> config = ParseBoardConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 0,
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, BoardConfigPersistedLogsTotalSizeKibNegative) {
  const std::optional<BoardConfig> config = ParseBoardConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": -1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, BoardConfigPersistedLogsTotalSizeKibNotNumber) {
  const std::optional<BoardConfig> config = ParseBoardConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": ""
})");

  EXPECT_FALSE(config.has_value());
}
TEST_F(ConfigTest, BoardConfigUseOverrideConfig) {
  const std::string override_path = WriteConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1
})");

  const std::optional<BoardConfig> config = GetBoardConfig(override_path, "/bad/path");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_num_files, 1u);
  EXPECT_EQ(config->persisted_logs_total_size, StorageSize::Kilobytes(1));
}

TEST_F(ConfigTest, BoardConfigUseDefaultConfig) {
  const std::string default_path = WriteConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1
})");

  const std::optional<BoardConfig> config = GetBoardConfig("/bad/path", default_path);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_num_files, 1u);
  EXPECT_EQ(config->persisted_logs_total_size, StorageSize::Kilobytes(1));
}

TEST_F(ConfigTest, BoardConfigMissingOverrideAndDefaultConfigs) {
  const std::optional<BoardConfig> config = GetBoardConfig("/bad/path", "/bad/path");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, GetCrashReportsConfig) {
  const std::string default_config_path = WriteConfig(R"({
    "crash_reporter": {
        "daily_per_product_quota": -1
    },
    "crash_server": {
        "upload_policy": "disabled"
    },
    "hourly_snapshot": false
})");

  const std::string override_config_path = WriteConfig(R"({
    "crash_reporter": {
        "daily_per_product_quota": 100
    },
    "crash_server" : {
        "upload_policy": "enabled"
    },
    "hourly_snapshot": true
})");

  const std::string invalid_config_path = WriteConfig(R"({
    "invalid": {}
})");
  //
  // The override config should be read regardless of the default config being valid.
  auto config = GetCrashReportsConfig("/bad/path", override_config_path);
  ASSERT_TRUE(config);
  EXPECT_EQ(config->crash_report_upload_policy, kUploadEnabled);
  EXPECT_EQ(config->daily_per_product_quota, 100u);
  EXPECT_EQ(config->hourly_snapshot, true);

  config = GetCrashReportsConfig(invalid_config_path, override_config_path);
  ASSERT_TRUE(config);
  EXPECT_EQ(config->crash_report_upload_policy, kUploadEnabled);
  EXPECT_EQ(config->daily_per_product_quota, 100u);
  EXPECT_EQ(config->hourly_snapshot, true);

  // The default config should be read if there's an issue using the override config.
  config = GetCrashReportsConfig(default_config_path, "/bad/path");
  ASSERT_TRUE(config);
  EXPECT_EQ(config->crash_report_upload_policy, kUploadDisabled);
  EXPECT_EQ(config->daily_per_product_quota, std::nullopt);
  EXPECT_EQ(config->hourly_snapshot, false);

  config = GetCrashReportsConfig(default_config_path, invalid_config_path);
  ASSERT_TRUE(config);
  EXPECT_EQ(config->crash_report_upload_policy, kUploadDisabled);
  EXPECT_EQ(config->daily_per_product_quota, std::nullopt);
  EXPECT_EQ(config->hourly_snapshot, false);

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
