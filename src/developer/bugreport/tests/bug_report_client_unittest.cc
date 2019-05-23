// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/bugreport/bug_report_client.h"

#include <gtest/gtest.h>

namespace bugreport {

namespace {

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

}  // namespace

TEST(BugReportClient, ValidDocument) {
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

TEST(BugReportClient, EdgeCases) {
  EXPECT_TRUE(HandleBugReport(kEmpty));
  EXPECT_FALSE(HandleBugReport("{{{{"));
  EXPECT_FALSE(HandleBugReport(kMissingAnnotations));
  EXPECT_FALSE(HandleBugReport(kMissingAttachments));
  EXPECT_FALSE(HandleBugReport(kWrongAnnotationType));
  EXPECT_FALSE(HandleBugReport(kWrongAttachmentType));
}

}  // namespace bugreport
