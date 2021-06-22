// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/developer/forensics/feedback_data/namespace_init.h"

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
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace feedback_data {
namespace {

using testing::BuildLogMessage;
using ::testing::UnorderedElementsAreArray;

MATCHER_P2(MatchesCobaltEvent, expected_type, expected_metric_id, "") {
  return arg.type == expected_type && arg.metric_id == expected_metric_id;
}

void WriteFile(const std::string& filepath, const std::string& content) {
  FX_CHECK(files::WriteFile(filepath, content.c_str(), content.size()));
}

std::string ReadFile(const std::string& filepath) {
  std::string content;
  FX_CHECK(files::ReadFileToString(filepath, &content));
  return content;
}

std::string MakeFilepath(const std::string& dir, const size_t file_num) {
  return files::JoinPath(dir, std::to_string(file_num));
}

const std::vector<std::string> CurrentLogFilePaths(const std::string& dir) {
  return {MakeFilepath(dir, 0), MakeFilepath(dir, 1), MakeFilepath(dir, 2), MakeFilepath(dir, 3),
          MakeFilepath(dir, 4), MakeFilepath(dir, 5), MakeFilepath(dir, 6), MakeFilepath(dir, 7)};
}

class NamespaceInitTest : public UnitTestFixture {
 public:
  NamespaceInitTest() : cobalt_(dispatcher(), services(), &clock_) {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
  }

 protected:
  timekeeper::TestClock clock_;
  cobalt::Logger cobalt_;
  files::ScopedTempDir temp_dir_;
};

TEST_F(NamespaceInitTest, CreatePreviousLogsFile) {
  std::string logs_dir;
  ASSERT_TRUE(temp_dir_.NewTempDir(&logs_dir));

  std::string previous_log_contents = "";
  for (const auto& filepath : CurrentLogFilePaths(logs_dir)) {
    auto encoder = system_log_recorder::ProductionEncoder();
    const std::string str = Format(BuildLogMessage(FX_LOG_INFO, "Log for file: " + filepath));
    previous_log_contents = previous_log_contents + str;
    WriteFile(filepath, encoder.Encode(str));
  }

  std::string log_file = files::JoinPath(temp_dir_.path(), "log.system.previous_boot.txt");
  CreatePreviousLogsFile(&cobalt_, logs_dir, log_file);

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

TEST_F(NamespaceInitTest, MoveAndRecordBootId) {
  const std::string current_boot_id_path = files::JoinPath(temp_dir_.path(), "current_boot_id.txt");
  const std::string previous_boot_id_path =
      files::JoinPath(temp_dir_.path(), "previous_boot_id.txt");

  const std::string previous_boot_id = "previous_boot_id";
  WriteFile(current_boot_id_path, previous_boot_id);

  MoveAndRecordBootId("current_boot_id", previous_boot_id_path, current_boot_id_path);

  EXPECT_EQ("previous_boot_id", ReadFile(previous_boot_id_path));
  EXPECT_EQ("current_boot_id", ReadFile(current_boot_id_path));
}

TEST_F(NamespaceInitTest, MoveAndRecordBuildVersion) {
  const std::string current_build_version_path =
      files::JoinPath(temp_dir_.path(), "current_build_version.txt");
  const std::string previous_build_version_path =
      files::JoinPath(temp_dir_.path(), "previous_build_version.txt");

  const std::string previous_build_version = "previous_build_version";
  WriteFile(current_build_version_path, previous_build_version);

  MoveAndRecordBuildVersion("current_build_version", previous_build_version_path,
                            current_build_version_path);

  EXPECT_EQ("previous_build_version", ReadFile(previous_build_version_path));
  EXPECT_EQ("current_build_version", ReadFile(current_build_version_path));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
