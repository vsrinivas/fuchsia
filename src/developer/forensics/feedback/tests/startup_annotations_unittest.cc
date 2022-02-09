// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/startup_annotations.h"

#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/feedback/reboot_log/annotations.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/scoped_memfs_manager.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace forensics::feedback {
namespace {

using ::testing::_;
using ::testing::Key;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

class StartupAnnotationsTest : public ::testing::Test {
 public:
  void SetUp() override {}

  void TearDown() override {}

  void WriteFile(const std::string& path, const std::string& data) {
    FX_CHECK(files::WriteFile(path, data)) << "Failed to write to " << path;
  }

  void WriteFiles(const std::map<std::string, std::string>& paths_and_data) {
    for (const auto& [path, data] : paths_and_data) {
      WriteFile(path, data);
    }
  }
};

TEST_F(StartupAnnotationsTest, Keys) {
  const RebootLog reboot_log(RebootReason::kOOM, "", std::nullopt, std::nullopt);
  const auto startup_annotations = GetStartupAnnotations(reboot_log);

  EXPECT_THAT(startup_annotations, UnorderedElementsAreArray({
                                       Key(kBuildBoardKey),
                                       Key(kBuildProductKey),
                                       Key(kBuildLatestCommitDateKey),
                                       Key(kBuildVersionKey),
                                       Key(kBuildVersionPreviousBootKey),
                                       Key(kBuildIsDebugKey),
                                       Key(kDeviceBoardNameKey),
                                       Key(kSystemBootIdCurrentKey),
                                       Key(kSystemBootIdPreviousKey),
                                       Key(kSystemLastRebootReasonKey),
                                       Key(kSystemLastRebootUptimeKey),
                                   }));
}

TEST_F(StartupAnnotationsTest, Values_FilesPresent) {
  testing::ScopedMemFsManager memfs_manager;

  memfs_manager.Create("/config/build-info");
  memfs_manager.Create("/cache");
  memfs_manager.Create("/data");
  memfs_manager.Create("/tmp");

  WriteFiles({
      {kBuildBoardPath, "board"},
      {kBuildProductPath, "product"},
      {kBuildCommitDatePath, "commit-date"},
      {kCurrentBuildVersionPath, "current-version"},
      {kPreviousBuildVersionPath, "previous-version"},
      {kCurrentBootIdPath, "current-boot-id"},
      {kPreviousBootIdPath, "previous-boot-id"},
  });

  const RebootLog reboot_log(RebootReason::kOOM, "", std::nullopt, std::nullopt);
  const auto startup_annotations = GetStartupAnnotations(reboot_log);

  EXPECT_THAT(
      startup_annotations,
      UnorderedElementsAre(
          Pair(kBuildBoardKey, "board"), Pair(kBuildProductKey, "product"),
          Pair(kBuildLatestCommitDateKey, "commit-date"), Pair(kBuildVersionKey, "current-version"),
          Pair(kBuildVersionPreviousBootKey, "previous-version"), Pair(kBuildIsDebugKey, _),
          Pair(kDeviceBoardNameKey, _), Pair(kSystemBootIdCurrentKey, "current-boot-id"),
          Pair(kSystemBootIdPreviousKey, "previous-boot-id"),
          Pair(kSystemLastRebootReasonKey, LastRebootReasonAnnotation(reboot_log)),
          Pair(kSystemLastRebootUptimeKey, LastRebootUptimeAnnotation(reboot_log))));
}

TEST_F(StartupAnnotationsTest, Values_FilesMissing) {
  const RebootLog reboot_log(RebootReason::kOOM, "", std::nullopt, std::nullopt);
  const auto startup_annotations = GetStartupAnnotations(reboot_log);

  EXPECT_THAT(
      startup_annotations,
      UnorderedElementsAre(
          Pair(kBuildBoardKey, Error::kFileReadFailure),
          Pair(kBuildProductKey, Error::kFileReadFailure),
          Pair(kBuildLatestCommitDateKey, Error::kFileReadFailure),
          Pair(kBuildVersionKey, Error::kFileReadFailure),
          Pair(kBuildVersionPreviousBootKey, Error::kFileReadFailure), Pair(kBuildIsDebugKey, _),
          Pair(kDeviceBoardNameKey, _), Pair(kSystemBootIdCurrentKey, Error::kFileReadFailure),
          Pair(kSystemBootIdPreviousKey, Error::kFileReadFailure),
          Pair(kSystemLastRebootReasonKey, LastRebootReasonAnnotation(reboot_log)),
          Pair(kSystemLastRebootUptimeKey, LastRebootUptimeAnnotation(reboot_log))));
}

}  // namespace
}  // namespace forensics::feedback
