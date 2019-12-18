// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/files/path.h"

#include "gtest/gtest.h"
#include "src/ledger/lib/files/directory.h"
#include "src/ledger/lib/files/scoped_tmp_dir.h"
#include "src/ledger/lib/files/unique_fd.h"

namespace ledger {
namespace {

TEST(Path, GetDirectoryName) {
  EXPECT_EQ("foo", GetDirectoryName("foo/"));
  EXPECT_EQ("foo/bar", GetDirectoryName("foo/bar/"));
  EXPECT_EQ("foo", GetDirectoryName("foo/bar"));
  EXPECT_EQ("foo/bar", GetDirectoryName("foo/bar/.."));
  EXPECT_EQ("foo/bar/..", GetDirectoryName("foo/bar/../.."));
  EXPECT_EQ("", GetDirectoryName("foo"));
  EXPECT_EQ("/", GetDirectoryName("/"));
  EXPECT_EQ("", GetDirectoryName("a"));
  EXPECT_EQ("/", GetDirectoryName("/a"));
  EXPECT_EQ("/a", GetDirectoryName("/a/"));
  EXPECT_EQ("a", GetDirectoryName("a/"));
}

TEST(Path, GetBaseName) {
  EXPECT_EQ("", GetBaseName("foo/"));
  EXPECT_EQ("", GetBaseName("foo/bar/"));
  EXPECT_EQ("bar", GetBaseName("foo/bar"));
  EXPECT_EQ("..", GetBaseName("foo/bar/.."));
  EXPECT_EQ("..", GetBaseName("foo/bar/../.."));
  EXPECT_EQ("foo", GetBaseName("foo"));
  EXPECT_EQ("", GetBaseName("/"));
  EXPECT_EQ("a", GetBaseName("a"));
  EXPECT_EQ("a", GetBaseName("/a"));
  EXPECT_EQ("", GetBaseName("/a/"));
  EXPECT_EQ("", GetBaseName("a/"));
}

TEST(Path, DeletePathAt) {
  ScopedTmpDir dir;
  unique_fd root(openat(dir.path().root_fd(), dir.path().path().c_str(), O_RDONLY));
  ASSERT_TRUE(root.is_valid());

  std::string sub_dir = "dir";
  CreateDirectoryAt(root.get(), sub_dir);
  EXPECT_TRUE(IsDirectoryAt(root.get(), sub_dir));
  EXPECT_TRUE(DeletePathAt(root.get(), sub_dir, false));
  EXPECT_FALSE(IsDirectoryAt(root.get(), sub_dir));
}

TEST(Path, DeletePathRecursivelyAt) {
  ScopedTmpDir dir;
  unique_fd root(openat(dir.path().root_fd(), dir.path().path().c_str(), O_RDONLY));
  ASSERT_TRUE(root.is_valid());

  std::string sub_dir = "dir";
  CreateDirectoryAt(root.get(), sub_dir);
  EXPECT_TRUE(IsDirectoryAt(root.get(), sub_dir));

  std::string sub_sub_dir1 = sub_dir + "/dir1";
  CreateDirectoryAt(root.get(), sub_sub_dir1);
  EXPECT_TRUE(IsDirectoryAt(root.get(), sub_sub_dir1));
  std::string sub_sub_dir2 = sub_dir + "/dir2";
  CreateDirectoryAt(root.get(), sub_sub_dir2);
  EXPECT_TRUE(IsDirectoryAt(root.get(), sub_sub_dir2));

  EXPECT_FALSE(DeletePathAt(root.get(), sub_dir, false));
  EXPECT_TRUE(IsDirectoryAt(root.get(), sub_dir));
  EXPECT_TRUE(IsDirectoryAt(root.get(), sub_sub_dir1));

  EXPECT_TRUE(DeletePathAt(root.get(), sub_dir, true));
  EXPECT_FALSE(IsDirectoryAt(root.get(), sub_dir));
  EXPECT_FALSE(IsDirectoryAt(root.get(), sub_sub_dir1));
}

}  // namespace
}  // namespace ledger
