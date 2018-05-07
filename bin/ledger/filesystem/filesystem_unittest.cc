// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/ledger/filesystem/directory_reader.h"
#include "peridot/bin/ledger/filesystem/get_directory_content_size.h"

#include <set>

#include "gtest/gtest.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/gtest/test_with_loop.h"

namespace ledger {
namespace {

using FilesystemHelpersTest = gtest::TestWithLoop;

const std::string kFileContent = "file content";

TEST_F(FilesystemHelpersTest, GetDirectoryEntries) {
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(files::CreateDirectory(temp_dir.path() + "/foo"));
  ASSERT_TRUE(files::WriteFile(temp_dir.path() + "/bar", kFileContent.data(),
                               kFileContent.size()));
  ASSERT_TRUE(files::WriteFile(temp_dir.path() + "/foo/baz",
                               kFileContent.data(), kFileContent.size()));

  std::set<std::string> expected_entries = {"foo", "bar"};
  EXPECT_TRUE(DirectoryReader::GetDirectoryEntries(
      temp_dir.path(), [&expected_entries](fxl::StringView entry) {
        auto entry_iterator = expected_entries.find(entry.ToString());
        EXPECT_NE(entry_iterator, expected_entries.end());
        expected_entries.erase(entry_iterator);
        return true;
      }));
  EXPECT_EQ(expected_entries.size(), 0u);
}

TEST_F(FilesystemHelpersTest, GetDirectoryContentSize) {
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(files::CreateDirectory(temp_dir.path() + "/foo"));
  ASSERT_TRUE(files::WriteFile(temp_dir.path() + "/bar", kFileContent.data(),
                               kFileContent.size()));
  ASSERT_TRUE(files::WriteFile(temp_dir.path() + "/foo/baz",
                               kFileContent.data(), kFileContent.size()));
  uint64_t directory_size = 0;
  ASSERT_TRUE(GetDirectoryContentSize(temp_dir.path(), &directory_size));
  ASSERT_EQ(directory_size, 2 * kFileContent.size());
}

}  // namespace
}  // namespace ledger
