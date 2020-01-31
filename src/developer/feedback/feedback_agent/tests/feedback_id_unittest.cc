// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/feedback_id.h"

#include <memory>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/uuid/uuid.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

constexpr char kDefaultFeedbackId[] = "00000000-0000-4000-a000-000000000001";

class FeedbackIdTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(uuid::IsValid(kDefaultFeedbackId));
    SetFeedbackIdFileContentsTo(kDefaultFeedbackId);
  }

  void SetFeedbackIdFileContentsTo(const std::string& contents) {
    ASSERT_TRUE(tmp_dir_.NewTempFileWithData(contents, &feedback_id_path_));
  }

  void CheckFeedbackIdFileContentsAre(const std::string& expected_contents) {
    std::string file_contents;
    ASSERT_TRUE(files::ReadFileToString(feedback_id_path_, &file_contents));
    EXPECT_EQ(file_contents, expected_contents);
  }

  void CheckFeedbackIdFileContentsAreValid() {
    std::string file_contents;
    ASSERT_TRUE(files::ReadFileToString(feedback_id_path_, &file_contents));
    EXPECT_TRUE(uuid::IsValid(file_contents));
  }

  void DeleteFeedbackIdFile() {
    ASSERT_TRUE(files::DeletePath(feedback_id_path_, /*recursive=*/false));
  }

  std::string feedback_id_path_;

 private:
  files::ScopedTempDir tmp_dir_;
};

TEST_F(FeedbackIdTest, LeaveFileUntouchedIfPresent) {
  EXPECT_TRUE(InitializeFeedbackId(feedback_id_path_));
  CheckFeedbackIdFileContentsAre(kDefaultFeedbackId);
}

TEST_F(FeedbackIdTest, CheckFileIfNotPresent) {
  DeleteFeedbackIdFile();
  EXPECT_TRUE(InitializeFeedbackId(feedback_id_path_));
  CheckFeedbackIdFileContentsAreValid();
}

TEST_F(FeedbackIdTest, OverwriteFileIfInvalid) {
  SetFeedbackIdFileContentsTo("INVALID ID");
  EXPECT_TRUE(InitializeFeedbackId(feedback_id_path_));
  CheckFeedbackIdFileContentsAreValid();
}

TEST_F(FeedbackIdTest, FailsIfPathIsADirectory) {
  DeleteFeedbackIdFile();
  ASSERT_TRUE(files::CreateDirectory(feedback_id_path_));
  EXPECT_FALSE(InitializeFeedbackId(feedback_id_path_));
}

}  // namespace
}  // namespace feedback
