// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/config.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::feedback {
namespace {

constexpr auto kUploadDisabled = crash_reports::Config::UploadPolicy::kDisabled;
constexpr auto kUploadEnabled = crash_reports::Config::UploadPolicy::kEnabled;

TEST(ConfigTest, GetCrashReportsConfig) {
  files::ScopedTempDir temp_dir;

  std::string default_config_path;
  ASSERT_TRUE(temp_dir.NewTempFileWithData(R"({
    "crash_reporter": {
        "daily_per_product_quota": -1
    },
    "crash_server": {
        "upload_policy": "disabled"
    },
    "hourly_snapshot": false
})",
                                           &default_config_path));

  std::string override_config_path;
  ASSERT_TRUE(temp_dir.NewTempFileWithData(R"({
    "crash_reporter": {
        "daily_per_product_quota": 100
    },
    "crash_server" : {
        "upload_policy": "enabled"
    },
    "hourly_snapshot": true
})",
                                           &override_config_path));

  std::string invalid_config_path;
  ASSERT_TRUE(temp_dir.NewTempFileWithData(R"({
    "invalid": {}
})",
                                           &invalid_config_path));

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

TEST(ConfigTest, GetFeedbackDataConfig) {
  files::ScopedTempDir temp_dir;

  std::string config_path;
  ASSERT_TRUE(temp_dir.NewTempFileWithData(R"({
    "annotation_allowlist": [
      "annotation_one",
      "annotation_two"
    ],
    "attachment_allowlist": [
      "attachment_one"
    ]
})",
                                           &config_path));

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
