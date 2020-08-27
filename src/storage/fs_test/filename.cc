// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <optional>
#include <string>

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>

#include "gtest/gtest.h"
#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/fs_test/misc.h"

namespace fs_test {
namespace {

using FilenameNotFatTest = FilesystemTest;
TEST_P(FilenameNotFatTest, TestOnlySpacePeriodNameSucceeds) {
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, "  .  ", false));
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, "  . ", false));
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, ".  . ", false));
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, ".  . .", false));
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, ".....", false));
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, "     ", false));
}

using FilenameTestFat = FilesystemTest;
TEST_P(FilenameTestFat, TestOnlySpacePeriodNameFails) {
  ASSERT_EQ(mkdir(GetPath("  . ").c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);
  ASSERT_EQ(mkdir(GetPath(".  . ").c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);
  ASSERT_EQ(mkdir(GetPath(".  . .").c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);
  ASSERT_EQ(mkdir(GetPath(".....").c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);
  ASSERT_EQ(mkdir(GetPath("     ").c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);
}

TEST_P(FilenameTestFat, TestTrailingDots) {
  ASSERT_EQ(mkdir(GetPath("hello...").c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);
  ASSERT_EQ(mkdir(GetPath("hello..").c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);
}

TEST_P(FilenameTestFat, TestLeadingTrailingSpaces) {
  // Note that the spec says that leading spaces should be ignored, but neither Linux or Windows
  // ignore them, so we expect them to be valid.
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, " foo", false));
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, "  foo", false));

  // Trailing spaces are invalid.
  ASSERT_EQ(mkdir(GetPath("foo  ").c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);
  ASSERT_EQ(mkdir(GetPath("foo ").c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, "foo", false));
}

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, FilenameNotFatTest,
    testing::ValuesIn(MapAndFilterAllTestFilesystems(
        [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
          if (options.filesystem->GetTraits().is_fat) {
            return std::nullopt;
          }
          return options;
        })),
    testing::PrintToStringParamName());

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, FilenameTestFat,
    testing::ValuesIn(MapAndFilterAllTestFilesystems(
        [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
          if (!options.filesystem->GetTraits().is_fat) {
            return std::nullopt;
          }
          return options;
        })),
    testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
