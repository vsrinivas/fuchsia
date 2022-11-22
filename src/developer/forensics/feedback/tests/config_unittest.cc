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

class ProductConfigTest : public ConfigTest {
 protected:
  std::optional<ProductConfig> ParseConfig(const std::string& config) {
    return GetProductConfig(WriteConfig(config));
  }
};

class BuildTypeConfigTest : public ConfigTest {
 protected:
  std::optional<BuildTypeConfig> ParseConfig(const std::string& config) {
    return GetBuildTypeConfig(WriteConfig(config));
  }
};

TEST_F(ProductConfigTest, MissingPersistedLogsNumFiles) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, MissingPersistedLogsTotalSizeKib) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, MissingSnapshotPersistenceMaxTmpSizeMib) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, MissingSnapshotPersistenceMaxCacheSizeMib) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, SpuriousField) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
  "spurious": ""
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, PersistedLogsNumFilesPositive) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_num_files, 1u);
}

TEST_F(ProductConfigTest, PersistedLogsNumFilesZero) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 0,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, PersistedLogsNumFilesNegative) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": -1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, PersistedLogsNumFilesNotNumber) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": "",
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, PersistedLogsTotalSizeKibPositive) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_total_size, StorageSize::Kilobytes(1));
}

TEST_F(ProductConfigTest, PersistedLogsTotalSizeKibZero) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 0,,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, PersistedLogsTotalSizeKibNegative) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": -1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, PersistedLogsTotalSizeKibNotNumber) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": "",
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, SnapshotPersistenceMaxTmpSizeMibPositive) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->snapshot_persistence_max_tmp_size, StorageSize::Megabytes(1));
}

TEST_F(ProductConfigTest, SnapshotPersistenceMaxTmpSizeMibZero) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 0,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->snapshot_persistence_max_tmp_size.has_value());
}

TEST_F(ProductConfigTest, SnapshotPersistenceMaxTmpSizeMibNegative) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": -1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->snapshot_persistence_max_tmp_size.has_value());
}

TEST_F(ProductConfigTest, SnapshotPersistenceMaxTmpSizeMibNotNumber) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": "",
  "snapshot_persistence_max_cache_size_mib": 1
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, SnapshotPersistenceMaxCacheSizeMibPositive) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->snapshot_persistence_max_cache_size, StorageSize::Megabytes(1));
}

TEST_F(ProductConfigTest, SnapshotPersistenceMaxCacheSizeMibZero) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 0
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->snapshot_persistence_max_cache_size.has_value());
}

TEST_F(ProductConfigTest, SnapshotPersistenceMaxCacheSizeMibNegative) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": -1
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->snapshot_persistence_max_cache_size.has_value());
}

TEST_F(ProductConfigTest, SnapshotPersistenceMaxCacheSizeMibNotNumber) {
  const std::optional<ProductConfig> config = ParseConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": ""
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(ProductConfigTest, UseOverrideConfig) {
  const std::string override_path = WriteConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  const std::optional<ProductConfig> config = GetProductConfig(override_path, "/bad/path");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_num_files, 1u);
  EXPECT_EQ(config->persisted_logs_total_size, StorageSize::Kilobytes(1));
  EXPECT_EQ(config->snapshot_persistence_max_tmp_size, StorageSize::Megabytes(1));
  EXPECT_EQ(config->snapshot_persistence_max_cache_size, StorageSize::Megabytes(1));
}

TEST_F(ProductConfigTest, UseDefaultConfig) {
  const std::string default_path = WriteConfig(R"({
  "persisted_logs_num_files": 1,
  "persisted_logs_total_size_kib": 1,
  "snapshot_persistence_max_tmp_size_mib": 1,
  "snapshot_persistence_max_cache_size_mib": 1
})");

  const std::optional<ProductConfig> config = GetProductConfig("/bad/path", default_path);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->persisted_logs_num_files, 1u);
  EXPECT_EQ(config->persisted_logs_total_size, StorageSize::Kilobytes(1));
  EXPECT_EQ(config->snapshot_persistence_max_tmp_size, StorageSize::Megabytes(1));
  EXPECT_EQ(config->snapshot_persistence_max_cache_size, StorageSize::Megabytes(1));
}

TEST_F(ProductConfigTest, MissingOverrideAndDefaultConfigs) {
  const std::optional<ProductConfig> config = GetProductConfig("/bad/path", "/bad/path");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, MissingCrashReportUploadPolicy) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, MissingDailyPerProductCrashReportQuota) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, MissingEnableDataRedaction) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, MissingEnableHourlySnapshots) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, MissingEnableLimitInspectData) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, SpuriousField) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false,
  "spurious": ""
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, CrashReportUploadPolicyDisabled) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->crash_report_upload_policy, CrashReportUploadPolicy::kDisabled);
}

TEST_F(BuildTypeConfigTest, CrashReportUploadPolicyEnabled) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "enabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->crash_report_upload_policy, CrashReportUploadPolicy::kEnabled);
}

TEST_F(BuildTypeConfigTest, CrashReportUploadPolicyReadFromPrivacySettings) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "read_from_privacy_settings",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->crash_report_upload_policy, CrashReportUploadPolicy::kReadFromPrivacySettings);
}

TEST_F(BuildTypeConfigTest, CrashReportUploadPolicyNotAllowedValue) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "not_allowed",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, CrashReportUploadPolicyNotString) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": 0,
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, DailyPerProductCrashReportQuotaNegative) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->daily_per_product_crash_report_quota, std::nullopt);
}

TEST_F(BuildTypeConfigTest, DailyPerProductCrashReportQuotaZero) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": 0,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->daily_per_product_crash_report_quota, std::nullopt);
}

TEST_F(BuildTypeConfigTest, DailyPerProductCrashReportQuotaPositive) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": 100,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->daily_per_product_crash_report_quota, 100);
}

TEST_F(BuildTypeConfigTest, DailyPerProductCrashReportQuotaNotNumber) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": "",
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, EnableDataRedactionTrue) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": true,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_data_redaction);
}

TEST_F(BuildTypeConfigTest, EnableDataRedactionFalse) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->enable_data_redaction);
}

TEST_F(BuildTypeConfigTest, EnableDataRedactionNotBoolean) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": "",
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, EnableHourlySnapshotsTrue) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": true,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_hourly_snapshots);
}

TEST_F(BuildTypeConfigTest, EnableHourlySnapshotsFalse) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->enable_hourly_snapshots);
}

TEST_F(BuildTypeConfigTest, EnableHourlySnapshotsNotBoolean) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": "",
  "enable_limit_inspect_data": false
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, EnableLimitInspectDataTrue) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": true
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enable_limit_inspect_data);
}

TEST_F(BuildTypeConfigTest, EnableLimitInspectDataFalse) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": false
})");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->enable_limit_inspect_data);
}

TEST_F(BuildTypeConfigTest, EnableLimitInspectDataNotBoolean) {
  const std::optional<BuildTypeConfig> config = ParseConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
  "enable_data_redaction": false,
  "enable_hourly_snapshots": false,
  "enable_limit_inspect_data": ""
})");

  EXPECT_FALSE(config.has_value());
}

TEST_F(BuildTypeConfigTest, UseOverrideBuildTypeConfig) {
  const std::string override_path = WriteConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
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

TEST_F(BuildTypeConfigTest, UseDefaultBuildTypeConfig) {
  const std::string default_path = WriteConfig(R"({
  "crash_report_upload_policy": "disabled",
  "daily_per_product_crash_report_quota": -1,
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

TEST_F(BuildTypeConfigTest, MissingOverrideAndDefaultBuildTypeConfigs) {
  const std::optional<BuildTypeConfig> config = GetBuildTypeConfig("/bad/path", "/bad/path");

  EXPECT_FALSE(config.has_value());
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
