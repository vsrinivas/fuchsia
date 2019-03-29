// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/strings/substitute.h"

namespace files {
namespace {

TEST(Directory, CreateDirectory) {
  std::string cwd = GetCurrentDirectory();

  ScopedTempDir dir;
  EXPECT_TRUE(IsDirectory(dir.path()));
  EXPECT_EQ(0, chdir(dir.path().c_str()));

  EXPECT_TRUE(CreateDirectory("foo/bar"));
  EXPECT_TRUE(IsDirectory("foo"));
  EXPECT_TRUE(IsDirectory("foo/bar"));
  EXPECT_FALSE(IsDirectory("foo/bar/baz"));

  EXPECT_TRUE(CreateDirectory("foo/bar/baz"));
  EXPECT_TRUE(IsDirectory("foo/bar/baz"));

  EXPECT_TRUE(CreateDirectory("qux"));
  EXPECT_TRUE(IsDirectory("qux"));

  EXPECT_EQ(0, chdir(cwd.c_str()));

  std::string abs_path = dir.path() + "/another/one";
  EXPECT_TRUE(CreateDirectory(abs_path));
  EXPECT_TRUE(IsDirectory(abs_path));
}

TEST(Directory, CreateDirectoryAt) {
  std::string cwd = GetCurrentDirectory();

  ScopedTempDir dir;
  EXPECT_TRUE(IsDirectory(dir.path()));
  fxl::UniqueFD root(open(dir.path().c_str(), O_RDONLY));
  EXPECT_TRUE(root.is_valid());
  EXPECT_FALSE(IsDirectoryAt(root.get(), "foo/bar/baz"));
  EXPECT_TRUE(CreateDirectoryAt(root.get(), "foo/bar/baz"));
  EXPECT_TRUE(IsDirectoryAt(root.get(), "foo/bar/baz"));
  EXPECT_TRUE(IsDirectory(dir.path() + "/foo/bar/baz"));
}

TEST(Directory, ReadDirContents) {
  ScopedTempDir dir;
  EXPECT_TRUE(IsDirectory(dir.path()));
  EXPECT_TRUE(CreateDirectory(fxl::Substitute("$0/foo", dir.path())));
  EXPECT_TRUE(CreateDirectory(fxl::Substitute("$0/bar", dir.path())));
  EXPECT_TRUE(CreateDirectory(fxl::Substitute("$0/baz", dir.path())));

  std::vector<std::string> contents;
  EXPECT_TRUE(ReadDirContents(dir.path(), &contents));
#if defined(OS_FUCHSIA)
  EXPECT_THAT(contents,
              ::testing::UnorderedElementsAre(".", "foo", "bar", "baz"));
#else
  EXPECT_THAT(contents,
              ::testing::UnorderedElementsAre(".", "..", "foo", "bar", "baz"));
#endif
  EXPECT_FALSE(ReadDirContents("bogus", &contents));
  EXPECT_EQ(errno, ENOENT);
}

}  // namespace
}  // namespace files
