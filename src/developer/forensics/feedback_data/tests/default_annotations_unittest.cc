// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/default_annotations.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::feedback_data {
namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

TEST(DefaultAnnotationsTest, GetCurrentBuildVersion) {
  files::ScopedTempDir temp_dir;
  std::string build_version_path;
  ASSERT_TRUE(temp_dir.NewTempFileWithData("current_build_version", &build_version_path));

  EXPECT_EQ(GetCurrentBuildVersion("/bad/path"), Error::kFileReadFailure);
  EXPECT_EQ(GetCurrentBuildVersion(build_version_path), "current_build_version");
}

TEST(DefaultAnnotationsTest, GetPreviousBuildVersion) {
  files::ScopedTempDir temp_dir;
  std::string build_version_path;
  ASSERT_TRUE(temp_dir.NewTempFileWithData("previous_build_version", &build_version_path));

  EXPECT_EQ(GetPreviousBuildVersion("/bad/path"), Error::kFileReadFailure);
  EXPECT_EQ(GetPreviousBuildVersion(build_version_path), "previous_build_version");
}

TEST(DefaultAnnotationsTest, GetCurrentBootId) {
  files::ScopedTempDir temp_dir;
  std::string boot_id_path;
  ASSERT_TRUE(temp_dir.NewTempFileWithData("current_boot_id", &boot_id_path));

  EXPECT_EQ(GetCurrentBootId("/bad/path"), Error::kFileReadFailure);
  EXPECT_EQ(GetCurrentBootId(boot_id_path), "current_boot_id");
}

TEST(DefaultAnnotationsTest, GetPreviousBootId) {
  files::ScopedTempDir temp_dir;
  std::string boot_id_path;
  ASSERT_TRUE(temp_dir.NewTempFileWithData("previous_boot_id", &boot_id_path));

  EXPECT_EQ(GetPreviousBootId("/bad/path"), Error::kFileReadFailure);
  EXPECT_EQ(GetPreviousBootId(boot_id_path), "previous_boot_id");
}

}  // namespace
}  // namespace forensics::feedback_data
