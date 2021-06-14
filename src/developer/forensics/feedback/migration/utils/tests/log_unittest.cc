// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/migration/utils/log.h"

#include <lib/syslog/cpp/macros.h>

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace feedback {
namespace {

TEST(MigrationLogTest, FromFile) {
  files::ScopedTempDir temp_dir;
  const std::string path = files::JoinPath(temp_dir.path(), "migration_log.txt");

  auto log = MigrationLog::FromFile(path);

  ASSERT_TRUE(log);
  EXPECT_FALSE(log->Contains(MigrationLog::Component::kLastReboot));
  EXPECT_FALSE(log->Contains(MigrationLog::Component::kCrashReports));
  EXPECT_FALSE(log->Contains(MigrationLog::Component::kFeedbackData));

  log->Set(MigrationLog::Component::kLastReboot);
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kLastReboot));
  EXPECT_FALSE(log->Contains(MigrationLog::Component::kCrashReports));
  EXPECT_FALSE(log->Contains(MigrationLog::Component::kFeedbackData));

  log = MigrationLog::FromFile(path);

  ASSERT_TRUE(log);
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kLastReboot));
  EXPECT_FALSE(log->Contains(MigrationLog::Component::kCrashReports));
  EXPECT_FALSE(log->Contains(MigrationLog::Component::kFeedbackData));

  log->Set(MigrationLog::Component::kCrashReports);
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kLastReboot));
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kCrashReports));
  EXPECT_FALSE(log->Contains(MigrationLog::Component::kFeedbackData));

  log = MigrationLog::FromFile(path);

  ASSERT_TRUE(log);
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kLastReboot));
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kCrashReports));
  EXPECT_FALSE(log->Contains(MigrationLog::Component::kFeedbackData));

  log->Set(MigrationLog::Component::kFeedbackData);
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kLastReboot));
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kCrashReports));
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kFeedbackData));

  log = MigrationLog::FromFile(path);

  ASSERT_TRUE(log);
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kLastReboot));
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kCrashReports));
  EXPECT_TRUE(log->Contains(MigrationLog::Component::kFeedbackData));
}

TEST(MigrationLogTest, Errors) {
  files::ScopedTempDir temp_dir;

  std::string path;
  temp_dir.NewTempDir(&path);

  ASSERT_FALSE(MigrationLog::FromFile(path));

  temp_dir.NewTempFileWithData("bad-formatting", &path);

  ASSERT_FALSE(MigrationLog::FromFile(path));
}

}  // namespace
}  // namespace feedback
}  // namespace forensics
