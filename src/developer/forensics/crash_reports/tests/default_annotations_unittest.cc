// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/default_annotations.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::crash_reports {
namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

TEST(DefaultAnnotationsTest, GetBuildVersion) {
  files::ScopedTempDir temp_dir;
  std::string build_version_path;
  ASSERT_TRUE(temp_dir.NewTempFileWithData("build_version", &build_version_path));

  EXPECT_EQ(GetBuildVersion("/bad/path"), Error::kFileReadFailure);
  EXPECT_EQ(GetBuildVersion(build_version_path), "build_version");
}

TEST(DefaultAnnotationsTest, GetDefaultAnnotations) {
  files::ScopedTempDir temp_dir;
  std::string build_version_path;
  ASSERT_TRUE(temp_dir.NewTempFileWithData("build_version", &build_version_path));

  std::string build_product_path;
  ASSERT_TRUE(temp_dir.NewTempFileWithData("build_product", &build_product_path));

  const auto default_annotations = GetDefaultAnnotations(
      /*build_version_path=*/build_version_path,
      /*build_board_path=*/"/bad/path",
      /*build_product_path=*/build_product_path,
      /*build_commit_date_path=*/"/bad/path");

  EXPECT_THAT(default_annotations.Raw(),
              UnorderedElementsAreArray({
                  Pair("osName", "Fuchsia"),
                  Pair("osVersion", "build_version"),
                  Pair("build.version", "build_version"),
                  Pair("build.board", "unknown"),
                  Pair("debug.build.board.error", "file read failure"),
                  Pair("build.product", "build_product"),
                  Pair("build.latest-commit-date", "unknown"),
                  Pair("debug.build.latest-commit-date.error", "file read failure"),
              }));
}

}  // namespace
}  // namespace forensics::crash_reports
