// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/static_attachments.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/scoped_memfs_manager.h"
#include "src/lib/files/file.h"

namespace forensics::feedback_data {
namespace {

using ::testing::Key;
using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

class StaticAttachmentsTest : public ::testing::Test {
 public:
  void WriteFiles(const std::map<std::string, std::string>& paths_and_data) {
    for (const auto& [path, data] : paths_and_data) {
      FX_CHECK(files::WriteFile(path, data)) << "Failed to write to " << path;
    }
  }
};

TEST_F(StaticAttachmentsTest, Keys) {
  EXPECT_THAT(GetStaticAttachments(), UnorderedElementsAreArray({
                                          Key(kAttachmentBuildSnapshot),
                                          Key(kAttachmentLogSystemPrevious),
                                      }));
}

TEST_F(StaticAttachmentsTest, FilesPresent) {
  testing::ScopedMemFsManager memfs_manager;

  memfs_manager.Create("/config/build-info");
  memfs_manager.Create("/tmp");

  WriteFiles({
      {"/config/build-info/snapshot", "build-info"},
      {kPreviousLogsFilePath, "previous-log"},
  });

  EXPECT_THAT(GetStaticAttachments(),
              UnorderedElementsAreArray({
                  Pair(kAttachmentBuildSnapshot, AttachmentValue("build-info")),
                  Pair(kAttachmentLogSystemPrevious, AttachmentValue("previous-log")),
              }));
}

TEST_F(StaticAttachmentsTest, FilesEmpty) {
  testing::ScopedMemFsManager memfs_manager;

  memfs_manager.Create("/config/build-info");
  memfs_manager.Create("/tmp");

  WriteFiles({
      {"/config/build-info/snapshot", ""},
      {kPreviousLogsFilePath, ""},
  });

  std::string data;
  ASSERT_TRUE(files::ReadFileToString("/config/build-info/snapshot", &data));
  ASSERT_EQ(data, "");

  EXPECT_THAT(GetStaticAttachments(),
              UnorderedElementsAreArray({
                  Pair(kAttachmentBuildSnapshot, AttachmentValue(Error::kMissingValue)),
                  Pair(kAttachmentLogSystemPrevious, AttachmentValue(Error::kMissingValue)),
              }));
}

TEST_F(StaticAttachmentsTest, FilesMissing) {
  EXPECT_THAT(GetStaticAttachments(),
              UnorderedElementsAreArray({
                  Pair(kAttachmentBuildSnapshot, AttachmentValue(Error::kFileReadFailure)),
                  Pair(kAttachmentLogSystemPrevious, AttachmentValue(Error::kFileReadFailure)),
              }));
}

}  // namespace
}  // namespace forensics::feedback_data
