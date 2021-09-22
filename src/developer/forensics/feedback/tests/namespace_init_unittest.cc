// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/namespace_init.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/logger.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/production_encoding.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/reader.h"
#include "src/developer/forensics/testing/log_message.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics::feedback {
namespace {

using testing::BuildLogMessage;
using ::testing::UnorderedElementsAreArray;

MATCHER_P2(MatchesCobaltEvent, expected_type, expected_metric_id, "") {
  return arg.type == expected_type && arg.metric_id == expected_metric_id;
}

std::string MakeFilepath(const std::string& dir, const size_t file_num) {
  return files::JoinPath(dir, std::to_string(file_num));
}

std::string MakeFilepath(const std::string& dir, const std::string& file) {
  return files::JoinPath(dir, file);
}

const std::vector<std::string> CurrentLogFilePaths(const std::string& dir) {
  return {MakeFilepath(dir, 0), MakeFilepath(dir, 1), MakeFilepath(dir, 2), MakeFilepath(dir, 3),
          MakeFilepath(dir, 4), MakeFilepath(dir, 5), MakeFilepath(dir, 6), MakeFilepath(dir, 7)};
}

class NamespaceInitTest : public UnitTestFixture {
 protected:
  std::string RootdDir() { return temp_dir_.path(); }

  void WriteFile(const std::string& path, const std::string& content) {
    FX_CHECK(files::WriteFile(path, content)) << "Failed to write to " << path;
  }

  void DeleteFile(const std::string& path) {
    FX_CHECK(files::DeletePath(path, /*recursive=*/true)) << "Failed to delete to " << path;
  }

  std::string ReadFile(const std::string& path) {
    std::string content;
    FX_CHECK(files::ReadFileToString(path, &content)) << "Failed to read from " << path;
    return content;
  }

  files::ScopedTempDir temp_dir_;
};

TEST_F(NamespaceInitTest, TestAndSetNotAFdr) {
  std::string path = MakeFilepath(RootdDir(), "not_a_fdr.txt");

  EXPECT_FALSE(TestAndSetNotAFdr(path));

  EXPECT_TRUE(TestAndSetNotAFdr(path));
  EXPECT_TRUE(TestAndSetNotAFdr(path));

  path = "/bad_path/not_a_fdr.txt";

  EXPECT_FALSE(TestAndSetNotAFdr(path));

  EXPECT_FALSE(TestAndSetNotAFdr(path));
  EXPECT_FALSE(TestAndSetNotAFdr(path));
}

TEST_F(NamespaceInitTest, MovePreviousRebootReason) {
  const std::string to = MakeFilepath(RootdDir(), "to.txt");
  const std::string from = MakeFilepath(RootdDir(), "from.txt");
  const std::string legacy_from = MakeFilepath(RootdDir(), "legacy_from.txt");

  // Neither |from| not |legacy_from| exists.
  MovePreviousRebootReason(from, legacy_from, to);
  EXPECT_FALSE(files::IsFile(to));

  // |to| can't be written to.
  WriteFile(from, "reboot_reason");
  MovePreviousRebootReason(from, legacy_from, "/bad_path/to.txt");
  EXPECT_FALSE(files::IsFile("/bad_path/to.txt"));
  EXPECT_TRUE(files::IsFile(from));
  EXPECT_EQ(ReadFile(from), "reboot_reason");

  // |from| works!
  WriteFile(from, "reboot_reason");
  MovePreviousRebootReason(from, legacy_from, to);
  EXPECT_FALSE(files::IsFile(from));
  EXPECT_TRUE(files::IsFile(to));
  EXPECT_EQ(ReadFile(to), "reboot_reason");

  // |legacy_from| works!
  DeleteFile(from);
  WriteFile(legacy_from, "reboot_reason");
  MovePreviousRebootReason(from, legacy_from, to);
  EXPECT_FALSE(files::IsFile(legacy_from));
  EXPECT_TRUE(files::IsFile(to));
  EXPECT_EQ(ReadFile(to), "reboot_reason");
}

TEST_F(NamespaceInitTest, MoveAndRecordBootId) {
  const std::string to = MakeFilepath(RootdDir(), "to.txt");
  const std::string from = MakeFilepath(RootdDir(), "from.txt");

  // |from| doesn't exist.
  MoveAndRecordBootId("boot-id-1", to, from);
  EXPECT_FALSE(files::IsFile(to));
  EXPECT_TRUE(files::IsFile(from));
  EXPECT_EQ(ReadFile(from), "boot-id-1");

  // |to| can't be written to.
  MoveAndRecordBootId("boot-id-2", "/bad-path/to.txt", from);
  EXPECT_FALSE(files::IsFile("/bad-path/to.txt"));
  EXPECT_TRUE(files::IsFile(from));
  EXPECT_EQ(ReadFile(from), "boot-id-2");

  // Everything works!
  WriteFile(from, "boot-id-3");
  MoveAndRecordBootId("boot-id-4", to, from);
  EXPECT_TRUE(files::IsFile(to));
  EXPECT_EQ(ReadFile(to), "boot-id-3");
  EXPECT_TRUE(files::IsFile(from));
  EXPECT_EQ(ReadFile(from), "boot-id-4");
}

TEST_F(NamespaceInitTest, MoveAndRecordBuildVersion) {
  const std::string to = MakeFilepath(RootdDir(), "to.txt");
  const std::string from = MakeFilepath(RootdDir(), "from.txt");

  // |from| doesn't exist.
  MoveAndRecordBuildVersion("build-version-1", to, from);
  EXPECT_FALSE(files::IsFile(to));
  EXPECT_TRUE(files::IsFile(from));
  EXPECT_EQ(ReadFile(from), "build-version-1");

  // |to| can't be written to.
  MoveAndRecordBuildVersion("build-version-2", "/bad-path/to.txt", from);
  EXPECT_FALSE(files::IsFile("/bad-path/to.txt"));
  EXPECT_TRUE(files::IsFile(from));
  EXPECT_EQ(ReadFile(from), "build-version-2");

  // Everything works!
  WriteFile(from, "build-version-3");
  MoveAndRecordBuildVersion("build-version-4", to, from);
  EXPECT_TRUE(files::IsFile(to));
  EXPECT_EQ(ReadFile(to), "build-version-3");
  EXPECT_TRUE(files::IsFile(from));
  EXPECT_EQ(ReadFile(from), "build-version-4");
}

TEST_F(NamespaceInitTest, CreatePreviousLogsFile) {
  timekeeper::TestClock clock;
  cobalt::Logger cobalt(dispatcher(), services(), &clock);
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

  std::string logs_dir;
  ASSERT_TRUE(temp_dir_.NewTempDir(&logs_dir));

  std::string previous_log_contents = "";
  for (const auto& filepath : CurrentLogFilePaths(logs_dir)) {
    auto encoder = feedback_data::system_log_recorder::ProductionEncoder();
    const std::string str = Format(BuildLogMessage(FX_LOG_INFO, "Log for file: " + filepath));
    previous_log_contents = previous_log_contents + str;
    WriteFile(filepath, encoder.Encode(str));
  }

  std::string log_file = MakeFilepath(RootdDir(), "log.system.previous_boot.txt");
  CreatePreviousLogsFile(&cobalt, logs_dir, log_file);

  RunLoopUntilIdle();

  EXPECT_FALSE(files::IsDirectory(logs_dir));
  EXPECT_EQ(previous_log_contents, ReadFile(log_file));

  // Verify the event type and metric_id.
  EXPECT_THAT(
      ReceivedCobaltEvents(),
      UnorderedElementsAreArray({
          MatchesCobaltEvent(cobalt::EventType::kInteger,
                             cobalt_registry::kPreviousBootLogCompressionRatioMigratedMetricId),
      }));
}

}  // namespace
}  // namespace forensics::feedback
