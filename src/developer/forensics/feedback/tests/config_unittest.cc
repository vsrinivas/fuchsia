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
 public:
  // Writes |config| to a file and returns the path of the config.
  std::string WriteConfig(const std::string& config) {
    std::string path;
    FX_CHECK(temp_dir_.NewTempFileWithData(config, &path));
    return path;
  }

 private:
  files::ScopedTempDir temp_dir_;
};

class BoardConfigTest : public ConfigTest {
 protected:
  std::optional<BoardConfig> ParseConfig(const std::string& config) {
    return GetBoardConfig(WriteConfig(config));
  }
};

class BuildTypeConfigTest : public ConfigTest {
 protected:
  std::optional<BuildTypeConfig> ParseConfig(const std::string& config) {
    return GetBuildTypeConfig(WriteConfig(config));
  }
};

TEST_F(BoardConfigTest, MissingPersistedLogsNumFiles) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, MissingPersistedLogsTotalSizeKib) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, MissingSnapshotPersistenceMaxTmpSizeMib) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, MissingSnapshotPersistenceMaxCacheSizeMib) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, SpuriousField) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
  "spurious": ""
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, PersistedLogsNumFilesPositive) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_num_files, 1u);
}

TEST_F(BoardConfigTest, PersistedLogsNumFilesZero) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 0,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, PersistedLogsNumFilesNegative) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": -1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, PersistedLogsNumFilesNotNumber) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": "",
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, PersistedLogsTotalSizeKibPositive) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_total_size, StorageSize::Kilobytes(1));
}

TEST_F(BoardConfigTest, PersistedLogsTotalSizeKibZero) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 0,,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, PersistedLogsTotalSizeKibNegative) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": -1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, PersistedLogsTotalSizeKibNotNumber) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": "",
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, SnapshotPersistenceMaxTmpSizeMibPositive) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->snapshot_persistence_max_tmp_size, StorageSize::Megabytes(1));
}

TEST_F(BoardConfigTest, SnapshotPersistenceMaxTmpSizeMibZero) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 0,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->snapshot_persistence_max_tmp_size.has_value());
}

TEST_F(BoardConfigTest, SnapshotPersistenceMaxTmpSizeMibNegative) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": -1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->snapshot_persistence_max_tmp_size.has_value());
}

TEST_F(BoardConfigTest, SnapshotPersistenceMaxTmpSizeMibNotNumber) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": "",
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, SnapshotPersistenceMaxCacheSizeMibPositive) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->snapshot_persistence_max_cache_size, StorageSize::Megabytes(1));
}

TEST_F(BoardConfigTest, SnapshotPersistenceMaxCacheSizeMibZero) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 0
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->snapshot_persistence_max_cache_size.has_value());
}

TEST_F(BoardConfigTest, SnapshotPersistenceMaxCacheSizeMibNegative) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": -1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->snapshot_persistence_max_cache_size.has_value());
}

TEST_F(BoardConfigTest, SnapshotPersistenceMaxCacheSizeMibNotNumber) {
  const std::optional<BoardConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": ""
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BoardConfigTest, UseOverrideConfig) {
  const std::string override_path = WriteConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  const std::optional<BoardConfig> config = GetBoardConfig(override_path, "/bad/path");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_num_files, 1u);
  EXPECT_EQ(config->persisted_logs_total_size, StorageSize::Kilobytes(1));
  EXPECT_EQ(config->snapshot_persistence_max_tmp_size, StorageSize::Megabytes(1));
  EXPECT_EQ(config->snapshot_persistence_max_cache_size, StorageSize::Megabytes(1));
}

TEST_F(BoardConfigTest, UseDefaultConfig) {
  const std::string default_path = WriteConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  const std::optional<BoardConfig> config = GetBoardConfig("/bad/path", default_path);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_num_files, 1u);
  EXPECT_EQ(config->persisted_logs_total_size, StorageSize::Kilobytes(1));
  EXPECT_EQ(config->snapshot_persistence_max_tmp_size, StorageSize::Megabytes(1));
  EXPECT_EQ(config->snapshot_persistence_max_cache_size, StorageSize::Megabytes(1));
}

TEST_F(BoardConfigTest, MissingOverrideAndDefaultConfigs) {
  const std::optional<BoardConfig> config = GetBoardConfig("/bad/path", "/bad/path");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, MissingEnableDataRedaction) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, MissingEnableLimitInspectData) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, SpuriousField) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_limit_inspect_data": false,
  "spurious": ""
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, EnableDataRedactionTrue) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": true,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_data_redaction);
}

TEST_F(BuildTypeConfigTest, EnableDataRedactionFalse) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->enable_data_redaction);
}

TEST_F(BuildTypeConfigTest, EnableDataRedactionNotBoolean) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": "",
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, EnableLimitInspectDataTrue) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_limit_inspect_data": true
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_limit_inspect_data);
}

TEST_F(BuildTypeConfigTest, EnableLimitInspectDataFalse) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->enable_limit_inspect_data);
}

TEST_F(BuildTypeConfigTest, EnableLimitInspectDataNotBoolean) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "enable_data_redaction": false,
  "enable_limit_inspect_data": ""
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, UseOverrideBuildTypeConfig) {
  const std::string override_path = WriteConfig(R"({
  "enable_data_redaction": true,
  "enable_limit_inspect_data": true
})");

  const std::optional<BuildTypeConfig> config = GetBuildTypeConfig(override_path, "/bad/path");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_data_redaction);
  EXPECT_TRUE(config->enable_limit_inspect_data);
}

TEST_F(BuildTypeConfigTest, UseDefaultBuildTypeConfig) {
  const std::string default_path = WriteConfig(R"({
  "enable_data_redaction": true,
  "enable_limit_inspect_data": true
})");

  const std::optional<BuildTypeConfig> config = GetBuildTypeConfig("/bad/path", default_path);

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_data_redaction);
  EXPECT_TRUE(config->enable_limit_inspect_data);
}

TEST_F(BuildTypeConfigTest, MissingOverrideAndDefaultBuildTypeConfigs) {
  const std::optional<BuildTypeConfig> config = GetBuildTypeConfig("/bad/path", "/bad/path");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, GetCrashReportsConfig) {
  const std::string default_config_path = WriteConfig(R"({
    "daily_per_product_quota": -1,
    "crash_report_upload_policy": "disabled",
    "hourly_snapshot": false
})");

  const std::string override_config_path = WriteConfig(R"({
    "daily_per_product_quota": 100,
    "crash_report_upload_policy": "enabled",
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
