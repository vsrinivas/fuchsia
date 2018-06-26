// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/filesystem/directory_reader.h"

#include <fcntl.h>
#include <set>

#include "gtest/gtest.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/strings/string_view.h"

namespace ledger {
namespace {

constexpr fxl::StringView kFileContent = "file content";

TEST(DirectoryReaderTest, GetDirectoryEntries) {
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

TEST(DirectoryReaderTest, GetDirectoryEntriesAt) {
  files::ScopedTempDir temp_dir;

  ASSERT_TRUE(files::CreateDirectory(temp_dir.path() + "/foo"));
  ASSERT_TRUE(files::WriteFile(temp_dir.path() + "/bar", kFileContent.data(),
                               kFileContent.size()));
  ASSERT_TRUE(files::WriteFile(temp_dir.path() + "/foo/baz",
                               kFileContent.data(), kFileContent.size()));

  std::set<std::string> expected_entries = {"foo", "bar"};

  int root_fd = open(temp_dir.path().c_str(), O_PATH);

  EXPECT_TRUE(DirectoryReader::GetDirectoryEntriesAt(
      DetachedPath(root_fd), [&expected_entries](fxl::StringView entry) {
        auto entry_iterator = expected_entries.find(entry.ToString());
        EXPECT_NE(entry_iterator, expected_entries.end());
        expected_entries.erase(entry_iterator);
        return true;
      }));
  EXPECT_EQ(expected_entries.size(), 0u);
}

}  // namespace
}  // namespace ledger
