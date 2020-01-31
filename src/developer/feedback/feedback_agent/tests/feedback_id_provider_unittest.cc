// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/feedback_id_provider.h"

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/uuid/uuid.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

constexpr char kDefaultFeedbackId[] = "00000000-0000-4000-a000-000000000001";

class FeedbackIdProviderTest : public testing::Test {
 protected:
  void SetUp() override { SetFeedbackIdFileContentsTo(kDefaultFeedbackId); }

  void TearDown() override { DeleteFeedbackIdFile(); }

  void SetFeedbackIdFileContentsTo(const std::string& contents) {
    ASSERT_TRUE(files::WriteFile(kFeedbackIdPath, contents.c_str(), contents.size()));
  }

  void DeleteFeedbackIdFile() {
    ASSERT_TRUE(files::DeletePath(kFeedbackIdPath, /*recursive=*/false));
  }

  std::optional<std::string> GetFeedbackId() {
    FeedbackIdProvider provider;
    return provider.GetAnnotation();
  }
};

TEST_F(FeedbackIdProviderTest, FileExists) {
  std::optional<std::string> feedback_id = GetFeedbackId();
  ASSERT_TRUE(feedback_id.has_value());
  EXPECT_EQ(feedback_id.value(), kDefaultFeedbackId);
}

TEST_F(FeedbackIdProviderTest, FailsIfFileDoesNotExist) {
  DeleteFeedbackIdFile();
  std::optional<std::string> feedback_id = GetFeedbackId();
  ASSERT_FALSE(feedback_id.has_value());
}

TEST_F(FeedbackIdProviderTest, FailsIfIdIsInvalid) {
  SetFeedbackIdFileContentsTo("BAD ID");
  std::optional<std::string> feedback_id = GetFeedbackId();
  ASSERT_FALSE(feedback_id.has_value());
}

TEST_F(FeedbackIdProviderTest, FailsIfPathIsADirectory) {
  DeleteFeedbackIdFile();
  files::CreateDirectory(kFeedbackIdPath);
  std::optional<std::string> feedback_id = GetFeedbackId();
  ASSERT_FALSE(feedback_id.has_value());
}

}  // namespace
}  // namespace feedback
