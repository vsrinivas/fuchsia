// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/bugreport/bug_report_client.h"

#include <gtest/gtest.h>

#include <fstream>

namespace bugreport {

namespace {

std::optional<std::string> ReadWholeFile(const std::filesystem::path& path) {
  std::ifstream ifs(path);
  if (!ifs.good())
    return std::nullopt;

  std::string tmp((std::istreambuf_iterator<char>(ifs)),
                  std::istreambuf_iterator<char>());
  return tmp;
}

constexpr char kValidDocument[] = R"(
  {
    "annotations":
    {
      "annotation.1.key": "annotation.1.value",
      "annotation.2.key": "annotation.2.value"
    },
    "attachments":
    {
      "attachment.1.key": "{\"embedded\": [\"array\"], \"another\": \"key\"}",
      "attachment.2.key": "attachment.2.value"
    }
  }
)";

constexpr char kEmpty[] = R"(
  {
    "annotations": { },
    "attachments": { }
  }
)";

constexpr char kMissingAnnotations[] = R"(
  {
    "attachments":
    {
      "attachment.1.key": "{\"embedded\": [\"json\", \"array\"]}",
      "attachment.2.key": "attachment.2.value"
    }
  }
)";

constexpr char kMissingAttachments[] = R"(
  {
    "annotations":
    {
      "annotation.1.key": "annotation.1.value",
      "annotation.2.key": "annotation.2.value"
    }
  }
)";

constexpr char kWrongAnnotationType[] = R"(
  {
    "annotations":
    {
      "annotation.1.key": {"not": "string"},
      "annotation.2.key": "annotation.2.value"
    },
    "attachments":
    {
      "attachment.1.key": "{\"embedded\": \"json\"}",
      "attachment.2.key": "attachment.2.value"
    }
  }
)";

constexpr char kWrongAttachmentType[] = R"(
  {
    "annotations":
    {
      "annotation.1.key": "annotation.1.value",
      "annotation.2.key": "annotation.2.value"
    },
    "attachments":
    {
      "attachment.1.key": {"not": "string"},
      "attachment.2.key": "attachment.2.value"
    }
  }
)";

class BugReportClientTest : public ::testing::Test {
 protected:
  void TearDown() override {
    // Best effort removal of test files.
    for (auto& target : targets_) {
      std::filesystem::remove(base_path_ / target.name);
    }
  }

  std::filesystem::path base_path_;
  std::vector<Target> targets_;
};

}  // namespace

TEST_F(BugReportClientTest, ValidDocument) {
  auto targets = HandleBugReport(kValidDocument);
  ASSERT_TRUE(targets);

  ASSERT_EQ(targets->size(), 3u);

  auto& annotation = targets->at(0);
  EXPECT_EQ(annotation.name, "annotations.json");
  EXPECT_EQ(annotation.contents,
            R"({
    "annotation.1.key": "annotation.1.value",
    "annotation.2.key": "annotation.2.value"
})");

  auto attachment1 = targets->at(1);
  EXPECT_EQ(attachment1.name, "attachment.1.key.json");
  EXPECT_EQ(attachment1.contents,
            R"({
    "embedded": [
        "array"
    ],
    "another": "key"
})");

  auto attachment2 = targets->at(2);
  EXPECT_EQ(attachment2.name, "attachment.2.key.txt");
  EXPECT_EQ(attachment2.contents, "attachment.2.value");
}

TEST_F(BugReportClientTest, EdgeCases) {
  EXPECT_TRUE(HandleBugReport(kEmpty));
  EXPECT_FALSE(HandleBugReport("{{{{"));
  EXPECT_FALSE(HandleBugReport(kMissingAnnotations));
  EXPECT_FALSE(HandleBugReport(kMissingAttachments));
  EXPECT_FALSE(HandleBugReport(kWrongAnnotationType));
  EXPECT_FALSE(HandleBugReport(kWrongAttachmentType));
}


TEST_F(BugReportClientTest, Export) {
  // Setup.
  std::error_code ec;
  auto test_path = std::filesystem::temp_directory_path(ec);
  ASSERT_FALSE(ec) << ec.message();

  auto targets = HandleBugReport(kValidDocument);
  ASSERT_TRUE(targets);
  ASSERT_EQ(targets->size(), 3u);

  base_path_ = test_path;
  targets_ = std::move(*targets);

  ASSERT_TRUE(Export(targets_, test_path));

  // Verify.
  std::optional<std::string> contents;

  contents = ReadWholeFile(base_path_ / targets_.at(0).name);
  if (!contents) {
    ADD_FAILURE() << "Error for: " << targets_.at(0).name;
  } else {
    EXPECT_EQ(*contents, targets_.at(0).contents);
  }

  contents = ReadWholeFile(base_path_ / targets_.at(1).name);
  if (!contents) {
    ADD_FAILURE() << "Error for: " << targets_.at(1).name;
  } else {
    EXPECT_EQ(*contents, targets_.at(1).contents);
  }

  contents = ReadWholeFile(base_path_ / targets_.at(2).name);
  if (!contents) {
    ADD_FAILURE() << "Error for: " << targets_.at(2).name;
  } else {
    EXPECT_EQ(*contents, targets_.at(2).contents);
  }
}

}  // namespace bugreport
