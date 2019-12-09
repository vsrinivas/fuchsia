// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/platform/platform.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace {

using testing::Gt;
using testing::StartsWith;
using testing::UnorderedElementsAre;

TEST(PlatformTest, WriteReadFile) {
  std::unique_ptr<Platform> platform = MakePlatform();

  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath path(tmpfs.root_fd(), "file");

  EXPECT_TRUE(platform->file_system()->WriteFile(path, "content"));

  std::string content;
  ASSERT_TRUE(platform->file_system()->ReadFileToString(path, &content));
  EXPECT_EQ(content, "content");
}

TEST(PlatformTest, IsFile) {
  std::unique_ptr<Platform> platform = MakePlatform();

  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath path(tmpfs.root_fd(), "file");

  EXPECT_TRUE(platform->file_system()->WriteFile(path, "content"));

  EXPECT_TRUE(platform->file_system()->IsFile(path));
}

TEST(PlatformTest, GetFileSize) {
  std::unique_ptr<Platform> platform = MakePlatform();

  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath path(tmpfs.root_fd(), "file");

  EXPECT_TRUE(platform->file_system()->WriteFile(path, "content"));

  uint64_t size;
  ASSERT_TRUE(platform->file_system()->GetFileSize(path, &size));
  EXPECT_THAT(size, Gt(0u));
}

TEST(PlatformTest, CreateDirectory) {
  std::unique_ptr<Platform> platform = MakePlatform();

  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath path(tmpfs.root_fd(), "base");

  EXPECT_TRUE(platform->file_system()->CreateDirectory(path));
  EXPECT_TRUE(platform->file_system()->IsDirectory(path));
}

TEST(PlatformTest, CreateDirectoryWithSubpaths) {
  std::unique_ptr<Platform> platform = MakePlatform();

  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath path(tmpfs.root_fd(), "base");
  DetachedPath subpath = path.SubPath("foo");
  ASSERT_EQ(subpath.root_fd(), path.root_fd());
  ASSERT_EQ(subpath.path(), "base/foo");

  EXPECT_TRUE(platform->file_system()->CreateDirectory(subpath));
  EXPECT_TRUE(platform->file_system()->IsDirectory(subpath));
}

TEST(PlatformTest, GetDirectoryContents) {
  std::unique_ptr<Platform> platform = MakePlatform();
  scoped_tmpfs::ScopedTmpFS tmpfs;
  std::string kFileContent = "file content";
  ASSERT_TRUE(platform->file_system()->CreateDirectory(DetachedPath(tmpfs.root_fd(), "foo")));
  ASSERT_TRUE(
      platform->file_system()->WriteFile(DetachedPath(tmpfs.root_fd(), "bar"), kFileContent));
  ASSERT_TRUE(
      platform->file_system()->WriteFile(DetachedPath(tmpfs.root_fd(), "foo/baz"), kFileContent));

  std::vector<std::string> contents;
  EXPECT_TRUE(
      platform->file_system()->GetDirectoryContents(DetachedPath(tmpfs.root_fd()), &contents));
  EXPECT_THAT(contents, UnorderedElementsAre("foo", "bar"));
}

TEST(PlatformTest, CreateScopedTmpDir) {
  std::unique_ptr<Platform> platform = MakePlatform();
  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath parent_path(tmpfs.root_fd(), "foo");
  ASSERT_TRUE(platform->file_system()->CreateDirectory(parent_path));

  std::unique_ptr<ScopedTmpDir> tmp_dir1 = platform->file_system()->CreateScopedTmpDir(parent_path);
  std::unique_ptr<ScopedTmpDir> tmp_dir2 = platform->file_system()->CreateScopedTmpDir(parent_path);

  // The created ScopedTmpDir should be under |parent_path|.
  ASSERT_NE(tmp_dir1, nullptr);
  DetachedPath path1 = tmp_dir1->path();
  EXPECT_THAT(path1.path(), StartsWith(parent_path.path()));
  EXPECT_EQ(path1.root_fd(), parent_path.root_fd());

  ASSERT_NE(tmp_dir2, nullptr);
  DetachedPath path2 = tmp_dir2->path();

  // The two ScopedTmpDirs should be diferrent.
  EXPECT_EQ(path1.root_fd(), path2.root_fd());
  EXPECT_NE(path1.path(), path2.path());
}

TEST(PlatformTest, DeletePathFile) {
  std::unique_ptr<Platform> platform = MakePlatform();

  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath path(tmpfs.root_fd(), "file");

  ASSERT_TRUE(platform->file_system()->WriteFile(path, "content"));
  EXPECT_TRUE(platform->file_system()->IsFile(path));

  // Check we can delete the file.
  EXPECT_TRUE(platform->file_system()->DeletePath(path));
  EXPECT_FALSE(platform->file_system()->IsFile(path));
}

TEST(PlatformTest, DeletePathDirectory) {
  std::unique_ptr<Platform> platform = MakePlatform();

  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath path(tmpfs.root_fd(), "base");
  DetachedPath subpath = path.SubPath("foo");
  ASSERT_EQ(subpath.root_fd(), path.root_fd());
  ASSERT_EQ(subpath.path(), "base/foo");

  EXPECT_TRUE(platform->file_system()->CreateDirectory(subpath));
  EXPECT_TRUE(platform->file_system()->IsDirectory(path));

  // Check we cannot delete the base directory since it has contents.
  EXPECT_FALSE(platform->file_system()->DeletePath(path));
  EXPECT_TRUE(platform->file_system()->IsDirectory(path));

  // But, we can delete the subpath "foo" as that one is empty.
  EXPECT_TRUE(platform->file_system()->DeletePath(subpath));
  EXPECT_FALSE(platform->file_system()->IsDirectory(subpath));
}

TEST(PlatformTest, DeletePathRecursivelyFile) {
  std::unique_ptr<Platform> platform = MakePlatform();

  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath path(tmpfs.root_fd(), "file");

  ASSERT_TRUE(platform->file_system()->WriteFile(path, "content"));
  EXPECT_TRUE(platform->file_system()->IsFile(path));

  // Check we can delete the file.
  EXPECT_TRUE(platform->file_system()->DeletePathRecursively(path));
  EXPECT_FALSE(platform->file_system()->IsFile(path));
}

TEST(PlatformTest, DeletePathRecursivelyDirectory) {
  std::unique_ptr<Platform> platform = MakePlatform();

  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath path(tmpfs.root_fd(), "base");
  DetachedPath subpath = path.SubPath("foo");
  ASSERT_EQ(subpath.root_fd(), path.root_fd());
  ASSERT_EQ(subpath.path(), "base/foo");

  EXPECT_TRUE(platform->file_system()->CreateDirectory(subpath));
  EXPECT_TRUE(platform->file_system()->IsDirectory(path));

  // Check we can delete the base directory and all its contents.
  EXPECT_TRUE(platform->file_system()->DeletePathRecursively(path));
  EXPECT_FALSE(platform->file_system()->IsDirectory(path));
  EXPECT_FALSE(platform->file_system()->IsDirectory(subpath));
}

}  // namespace
}  // namespace ledger
