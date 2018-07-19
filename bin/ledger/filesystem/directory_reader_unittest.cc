// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/filesystem/directory_reader.h"

#include <fcntl.h>
#include <set>

#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/strings/string_view.h>

#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace {

constexpr fxl::StringView kFileContent = "file content";

TEST(DirectoryReaderTest, GetDirectoryEntries) {
  scoped_tmpfs::ScopedTmpFS tmpfs;

  ASSERT_TRUE(files::CreateDirectoryAt(tmpfs.root_fd(), "foo"));
  ASSERT_TRUE(files::WriteFileAt(tmpfs.root_fd(), "bar", kFileContent.data(),
                                 kFileContent.size()));
  ASSERT_TRUE(files::WriteFileAt(tmpfs.root_fd(), "foo/baz",
                                 kFileContent.data(), kFileContent.size()));

  std::set<std::string> expected_entries = {"foo", "bar"};

  EXPECT_TRUE(GetDirectoryEntries(
      DetachedPath(tmpfs.root_fd()),
      [&expected_entries](fxl::StringView entry) {
        auto entry_iterator = expected_entries.find(entry.ToString());
        EXPECT_NE(entry_iterator, expected_entries.end());
        expected_entries.erase(entry_iterator);
        return true;
      }));
  EXPECT_EQ(expected_entries.size(), 0u);
}

}  // namespace
}  // namespace ledger
