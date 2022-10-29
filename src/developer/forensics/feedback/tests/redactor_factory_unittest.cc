// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/redactor_factory.h"

#include <lib/inspect/cpp/vmo/types.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include <string>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::feedback {
namespace {

using inspect::testing::BoolIs;
using inspect::testing::ChildrenMatch;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::UintIs;
using testing::AllOf;
using testing::ElementsAreArray;
using testing::IsEmpty;

constexpr std::string_view kUnredacted = "8.8.8.8";
constexpr std::string_view kRedacted = "<REDACTED-IPV4: 11>";

using RedactorFromConfigTest = UnitTestFixture;

TEST_F(RedactorFromConfigTest, FileMissing) {
  std::unique_ptr<RedactorBase> redactor = RedactorFromConfig(nullptr, "missing");

  std::string text(kUnredacted);
  EXPECT_EQ(redactor->Redact(text), text);
  EXPECT_THAT(InspectTree(), NodeMatches(AllOf(PropertyList(IsEmpty()))));

  redactor = RedactorFromConfig(&InspectRoot(), "missing");
  EXPECT_THAT(InspectTree(), NodeMatches(AllOf(PropertyList(ElementsAreArray({
                                 BoolIs("redaction_enabled", false),
                             })))));
}

TEST_F(RedactorFromConfigTest, FilePresent) {
  files::ScopedTempDir temp_dir;

  std::string path;
  ASSERT_TRUE(temp_dir.NewTempFile(&path));

  std::unique_ptr<RedactorBase> redactor;
  redactor = RedactorFromConfig(&InspectRoot(), path, [] { return 10; });

  std::string text(kUnredacted);
  EXPECT_EQ(redactor->Redact(text), kRedacted);
  EXPECT_THAT(InspectTree(), NodeMatches(AllOf(PropertyList(ElementsAreArray({
                                 BoolIs("redaction_enabled", true),
                                 UintIs("num_redaction_ids", 1u),
                             })))));

  redactor = RedactorFromConfig(nullptr, path, [] { return 10; });
  text = kUnredacted;
  EXPECT_EQ(redactor->Redact(text), kRedacted);
}

}  // namespace
}  // namespace forensics::feedback
