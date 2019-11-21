// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/filesystem/directory_reader.h"

#include <fcntl.h>

#include <set>

#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/unique_fd.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

constexpr absl::string_view kFileContent = "file content";

TEST(DirectoryReaderTest, GetDirectoryEntries) {
  scoped_tmpfs::ScopedTmpFS tmpfs;

  ASSERT_TRUE(files::CreateDirectoryAt(tmpfs.root_fd(), "foo"));
  ASSERT_TRUE(files::WriteFileAt(tmpfs.root_fd(), "bar", kFileContent.data(), kFileContent.size()));
  ASSERT_TRUE(
      files::WriteFileAt(tmpfs.root_fd(), "foo/baz", kFileContent.data(), kFileContent.size()));

  std::set<std::string> expected_entries = {"foo", "bar"};

  EXPECT_TRUE(GetDirectoryEntries(
      DetachedPath(tmpfs.root_fd()), [&expected_entries](absl::string_view entry) {
        auto entry_iterator = expected_entries.find(convert::ToString(entry));
        EXPECT_NE(entry_iterator, expected_entries.end());
        expected_entries.erase(entry_iterator);
        return true;
      }));
  EXPECT_TRUE(expected_entries.empty());
}

}  // namespace
}  // namespace ledger
