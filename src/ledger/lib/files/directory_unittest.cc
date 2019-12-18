// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/files/directory.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/lib/files/scoped_tmp_dir.h"
#include "src/ledger/lib/files/unique_fd.h"
#include "src/ledger/lib/logging/logging.h"

namespace ledger {
namespace {

using ::testing::UnorderedElementsAre;

TEST(Directory, CreateDirectoryAt) {
  ScopedTmpDir dir;
  EXPECT_TRUE(IsDirectoryAt(dir.path().root_fd(), dir.path().path()));

  unique_fd root(openat(dir.path().root_fd(), dir.path().path().c_str(), O_RDONLY));
  ASSERT_TRUE(root.is_valid());

  EXPECT_TRUE(root.is_valid());
  EXPECT_FALSE(IsDirectoryAt(root.get(), "foo/bar/baz"));
  EXPECT_TRUE(CreateDirectoryAt(root.get(), "foo/bar/baz"));
  EXPECT_TRUE(IsDirectoryAt(root.get(), "foo/bar/baz"));
}

TEST(Directory, ReadDirContentsAt) {
  ScopedTmpDir dir;
  DetachedPath dir_path = dir.path();
  EXPECT_TRUE(IsDirectoryAt(dir_path.root_fd(), dir_path.path()));
  EXPECT_TRUE(CreateDirectoryAt(dir_path.root_fd(), dir_path.path() + "/foo"));
  EXPECT_TRUE(CreateDirectoryAt(dir_path.root_fd(), dir_path.path() + "/bar"));
  EXPECT_TRUE(CreateDirectoryAt(dir_path.root_fd(), dir_path.path() + "/baz"));

  unique_fd root(openat(dir_path.root_fd(), dir_path.path().c_str(), O_RDONLY));
  ASSERT_TRUE(root.is_valid());

  std::vector<std::string> contents;
  EXPECT_TRUE(ReadDirContentsAt(root.get(), ".", &contents));
#if defined(__Fuchsia__)
  EXPECT_THAT(contents, UnorderedElementsAre(".", "foo", "bar", "baz"));
#else
  EXPECT_THAT(contents, UnorderedElementsAre(".", "..", "foo", "bar", "baz"));
#endif
  EXPECT_FALSE(ReadDirContentsAt(root.get(), "bogus", &contents));
  EXPECT_EQ(errno, ENOENT);
}

}  // namespace
}  // namespace ledger
