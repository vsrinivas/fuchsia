// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/filesystem/directory_reader.h"

#include <set>

#include "gtest/gtest.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"

namespace ledger {
namespace {

const std::string kFileContent = "file content";

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

}  // namespace
}  // namespace ledger
