// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/redactor_factory.h"

#include <string>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::feedback {
namespace {

constexpr std::string_view kUnredacted = "8.8.8.8";
constexpr std::string_view kRedacted = "<REDACTED-IPV4: 11>";

TEST(RedactorFromConfig, FileMissing) {
  auto redactor = RedactorFromConfig("missing");

  std::string text(kUnredacted);
  EXPECT_EQ(redactor->Redact(text), text);
}

TEST(RedactorFromConfig, FilePresent) {
  files::ScopedTempDir temp_dir;

  std::string path;
  ASSERT_TRUE(temp_dir.NewTempFile(&path));

  auto redactor = RedactorFromConfig(path, [] { return 10; });

  std::string text(kUnredacted);
  EXPECT_EQ(redactor->Redact(text), kRedacted);
}

}  // namespace
}  // namespace forensics::feedback
