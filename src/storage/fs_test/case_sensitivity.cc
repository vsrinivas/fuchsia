// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <zircon/errors.h>

#include <string_view>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/fs_test/misc.h"

namespace fs_test {
namespace {

using CaseInsensitiveTest = FilesystemTest;

// Deliberately chosen so we stray outside of ASCII.
constexpr std::string_view lower_name = "fo\xc3\xb2";
constexpr std::string_view upper_name = "fo\xc3\x92";

TEST_P(CaseInsensitiveTest, OpenUpperFromLowerSucceeds) {
  auto fd = fbl::unique_fd(open(GetPath(lower_name).c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  EXPECT_TRUE(fd);
  auto fd2 = fbl::unique_fd(open(GetPath(upper_name).c_str(), O_RDWR));
  EXPECT_TRUE(fd2);
}

TEST_P(CaseInsensitiveTest, OpenLowerFromUpperSucceeds) {
  auto fd = fbl::unique_fd(open(GetPath(upper_name).c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  EXPECT_TRUE(fd);
  auto fd2 = fbl::unique_fd(open(GetPath(lower_name).c_str(), O_RDWR));
  EXPECT_TRUE(fd2);
}

TEST_P(CaseInsensitiveTest, OpenUpperFromLowerNoCacheSucceeds) {
  {
    auto fd = fbl::unique_fd(open(GetPath(lower_name).c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
    EXPECT_TRUE(fd);
  }
  if (!fs().GetTraits().in_memory) {
    EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  }
  {
    auto fd = fbl::unique_fd(open(GetPath(upper_name).c_str(), O_RDWR));
    EXPECT_TRUE(fd);
  }
}

TEST_P(CaseInsensitiveTest, OpenLowerFromUpperNoCacheSucceeds) {
  {
    auto fd = fbl::unique_fd(open(GetPath(upper_name).c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
    EXPECT_TRUE(fd);
  }
  if (!fs().GetTraits().in_memory) {
    EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  }
  {
    auto fd = fbl::unique_fd(open(GetPath(lower_name).c_str(), O_RDWR));
    EXPECT_TRUE(fd);
  }
}

TEST_P(CaseInsensitiveTest, RenameLowerToUpperSucceeds) {
  {
    auto fd = fbl::unique_fd(open(GetPath(lower_name).c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
    EXPECT_TRUE(fd);
  }
  EXPECT_EQ(rename(GetPath(lower_name).c_str(), GetPath(upper_name).c_str()), 0) << strerror(errno);
  if (!fs().GetTraits().in_memory) {
    EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  }

  // Check the new name is what we get from readdir().
  ExpectedDirectoryEntry dir[] = {
      {".", DT_DIR},
      {upper_name, DT_REG},
  };

  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("").c_str(), dir));

  // Check that we can open the file with lower and upper names.
  auto fd = fbl::unique_fd(open(GetPath(lower_name).c_str(), O_RDWR));
  EXPECT_TRUE(fd);
  fd = fbl::unique_fd(open(GetPath(upper_name).c_str(), O_RDWR));
  EXPECT_TRUE(fd);
}

TEST_P(CaseInsensitiveTest, RenameUpperToLowerSucceeds) {
  {
    auto fd = fbl::unique_fd(open(GetPath(upper_name).c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
    EXPECT_TRUE(fd);
  }
  EXPECT_EQ(rename(GetPath(upper_name).c_str(), GetPath(lower_name).c_str()), 0) << strerror(errno);
  if (!fs().GetTraits().in_memory) {
    EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  }

  // Check the new name is what we get from readdir().
  ExpectedDirectoryEntry dir[] = {
      {".", DT_DIR},
      {lower_name, DT_REG},
  };

  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("").c_str(), dir));

  // Check that we can open the file with lower and upper names.
  auto fd = fbl::unique_fd(open(GetPath(lower_name).c_str(), O_RDWR));
  EXPECT_TRUE(fd);
  fd = fbl::unique_fd(open(GetPath(upper_name).c_str(), O_RDWR));
  EXPECT_TRUE(fd);
}

using CaseSensitiveTest = FilesystemTest;

TEST_P(CaseSensitiveTest, OpenSameFileDifferentCaseFails) {
  auto fd = fbl::unique_fd(open(GetPath(lower_name).c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  EXPECT_TRUE(fd);
  auto fd2 = fbl::unique_fd(open(GetPath(upper_name).c_str(), O_RDWR));
  EXPECT_FALSE(fd2);
}

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, CaseInsensitiveTest,
    testing::ValuesIn(MapAndFilterAllTestFilesystems(
        [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
          if (options.filesystem->GetTraits().is_case_sensitive) {
            return std::nullopt;
          } else {
            return options;
          }
        })),
    testing::PrintToStringParamName());

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CaseInsensitiveTest);

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, CaseSensitiveTest,
    testing::ValuesIn(MapAndFilterAllTestFilesystems(
        [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
          if (options.filesystem->GetTraits().is_case_sensitive) {
            return options;
          } else {
            return std::nullopt;
          }
        })),
    testing::PrintToStringParamName());

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CaseSensitiveTest);

}  // namespace
}  // namespace fs_test
