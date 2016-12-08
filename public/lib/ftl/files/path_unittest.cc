// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/scoped_temp_dir.h"

namespace files {
namespace {

TEST(Path, SimplifyPath) {
  EXPECT_EQ(".", SimplifyPath(""));
  EXPECT_EQ(".", SimplifyPath("."));
  EXPECT_EQ("..", SimplifyPath(".."));
  EXPECT_EQ("...", SimplifyPath("..."));

  EXPECT_EQ("/", SimplifyPath("/"));
  EXPECT_EQ("/", SimplifyPath("/."));
  EXPECT_EQ("/", SimplifyPath("/.."));
  EXPECT_EQ("/...", SimplifyPath("/..."));

  EXPECT_EQ("foo", SimplifyPath("foo"));
  EXPECT_EQ("foo", SimplifyPath("foo/"));
  EXPECT_EQ("foo", SimplifyPath("foo/."));
  EXPECT_EQ("foo", SimplifyPath("foo/./"));
  EXPECT_EQ(".", SimplifyPath("foo/.."));
  EXPECT_EQ(".", SimplifyPath("foo/../"));
  EXPECT_EQ("foo/...", SimplifyPath("foo/..."));
  EXPECT_EQ("foo/...", SimplifyPath("foo/.../"));
  EXPECT_EQ("foo/.b", SimplifyPath("foo/.b"));
  EXPECT_EQ("foo/.b", SimplifyPath("foo/.b/"));

  EXPECT_EQ("/foo", SimplifyPath("/foo"));
  EXPECT_EQ("/foo", SimplifyPath("/foo/"));
  EXPECT_EQ("/foo", SimplifyPath("/foo/."));
  EXPECT_EQ("/foo", SimplifyPath("/foo/./"));
  EXPECT_EQ("/", SimplifyPath("/foo/.."));
  EXPECT_EQ("/", SimplifyPath("/foo/../"));
  EXPECT_EQ("/foo/...", SimplifyPath("/foo/..."));
  EXPECT_EQ("/foo/...", SimplifyPath("/foo/.../"));
  EXPECT_EQ("/foo/.b", SimplifyPath("/foo/.b"));
  EXPECT_EQ("/foo/.b", SimplifyPath("/foo/.b/"));

  EXPECT_EQ("foo/bar", SimplifyPath("foo/bar"));
  EXPECT_EQ("foo/bar", SimplifyPath("foo/bar/"));
  EXPECT_EQ("foo/bar", SimplifyPath("foo/./bar"));
  EXPECT_EQ("foo/bar", SimplifyPath("foo/./bar/"));
  EXPECT_EQ("bar", SimplifyPath("foo/../bar"));
  EXPECT_EQ("bar", SimplifyPath("foo/baz/../../bar"));
  EXPECT_EQ("bar", SimplifyPath("foo/../bar/"));
  EXPECT_EQ("foo/.../bar", SimplifyPath("foo/.../bar"));
  EXPECT_EQ("foo/.../bar", SimplifyPath("foo/.../bar/"));
  EXPECT_EQ("foo/.b/bar", SimplifyPath("foo/.b/bar"));
  EXPECT_EQ("foo/.b/bar", SimplifyPath("foo/.b/bar/"));

  EXPECT_EQ("/foo/bar", SimplifyPath("/foo/bar"));
  EXPECT_EQ("/foo/bar", SimplifyPath("/foo/bar/"));
  EXPECT_EQ("/foo/bar", SimplifyPath("/foo/./bar"));
  EXPECT_EQ("/foo/bar", SimplifyPath("/foo/./bar/"));
  EXPECT_EQ("/bar", SimplifyPath("/foo/../bar"));
  EXPECT_EQ("/bar", SimplifyPath("/foo/../bar/"));
  EXPECT_EQ("/foo/.../bar", SimplifyPath("/foo/.../bar"));
  EXPECT_EQ("/foo/.../bar", SimplifyPath("/foo/.../bar/"));
  EXPECT_EQ("/foo/.b/bar", SimplifyPath("/foo/.b/bar"));
  EXPECT_EQ("/foo/.b/bar", SimplifyPath("/foo/.b/bar/"));

  EXPECT_EQ("../foo", SimplifyPath("../foo"));
  EXPECT_EQ("../../bar", SimplifyPath("../foo/../../bar"));
  EXPECT_EQ("/bar", SimplifyPath("/foo/../../bar"));

  // Already clean
  EXPECT_EQ(".", SimplifyPath(""));
  EXPECT_EQ("abc", SimplifyPath("abc"));
  EXPECT_EQ("abc/def", SimplifyPath("abc/def"));
  EXPECT_EQ("a/b/c", SimplifyPath("a/b/c"));
  EXPECT_EQ(".", SimplifyPath("."));
  EXPECT_EQ("..", SimplifyPath(".."));
  EXPECT_EQ("../..", SimplifyPath("../.."));
  EXPECT_EQ("../../abc", SimplifyPath("../../abc"));
  EXPECT_EQ("/abc", SimplifyPath("/abc"));
  EXPECT_EQ("/", SimplifyPath("/"));

  // Remove trailing slash
  EXPECT_EQ("abc", SimplifyPath("abc/"));
  EXPECT_EQ("abc/def", SimplifyPath("abc/def/"));
  EXPECT_EQ("a/b/c", SimplifyPath("a/b/c/"));
  EXPECT_EQ(".", SimplifyPath("./"));
  EXPECT_EQ("..", SimplifyPath("../"));
  EXPECT_EQ("../..", SimplifyPath("../../"));
  EXPECT_EQ("/abc", SimplifyPath("/abc/"));

  // Remove doubled slash
  EXPECT_EQ("abc/def/ghi", SimplifyPath("abc//def//ghi"));
  EXPECT_EQ("/abc", SimplifyPath("//abc"));
  EXPECT_EQ("/abc", SimplifyPath("///abc"));
  EXPECT_EQ("/abc", SimplifyPath("//abc//"));
  EXPECT_EQ("abc", SimplifyPath("abc//"));

  // Remove . elements
  EXPECT_EQ("abc/def", SimplifyPath("abc/./def"));
  EXPECT_EQ("/abc/def", SimplifyPath("/./abc/def"));
  EXPECT_EQ("abc", SimplifyPath("abc/."));

  // Remove .. elements
  EXPECT_EQ("abc/def/jkl", SimplifyPath("abc/def/ghi/../jkl"));
  EXPECT_EQ("abc/jkl", SimplifyPath("abc/def/../ghi/../jkl"));
  EXPECT_EQ("abc", SimplifyPath("abc/def/.."));
  EXPECT_EQ(".", SimplifyPath("abc/def/../.."));
  EXPECT_EQ("/", SimplifyPath("/abc/def/../.."));
  EXPECT_EQ("..", SimplifyPath("abc/def/../../.."));
  EXPECT_EQ("/", SimplifyPath("/abc/def/../../.."));
  EXPECT_EQ("../../mno", SimplifyPath("abc/def/../../../ghi/jkl/../../../mno"));

  // Combinations
  EXPECT_EQ("def", SimplifyPath("abc/./../def"));
  EXPECT_EQ("def", SimplifyPath("abc//./../def"));
  EXPECT_EQ("../../def", SimplifyPath("abc/../../././../def"));
}

TEST(Path, AbsolutePath) {
  EXPECT_EQ("/foo/bar", AbsolutePath("/foo/bar"));
  EXPECT_EQ("/foo/bar/", AbsolutePath("/foo/bar/"));
  EXPECT_EQ(GetCurrentDirectory(), AbsolutePath(""));
  EXPECT_EQ(GetCurrentDirectory() + "/foo", AbsolutePath("foo"));
}

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

TEST(Path, DeletePath) {
  ScopedTempDir dir;

  std::string sub_dir = dir.path() + "/dir";
  CreateDirectory(sub_dir);
  EXPECT_TRUE(IsDirectory(sub_dir));
  EXPECT_TRUE(DeletePath(sub_dir, false));
  EXPECT_FALSE(IsDirectory(sub_dir));
}

TEST(Path, DeletePathRecursively) {
  ScopedTempDir dir;

  std::string sub_dir = dir.path() + "/dir";
  CreateDirectory(sub_dir);
  EXPECT_TRUE(IsDirectory(sub_dir));

  std::string sub_sub_dir1 = sub_dir + "/dir1";
  CreateDirectory(sub_sub_dir1);
  EXPECT_TRUE(IsDirectory(sub_sub_dir1));
  std::string sub_sub_dir2 = sub_dir + "/dir2";
  CreateDirectory(sub_sub_dir2);
  EXPECT_TRUE(IsDirectory(sub_sub_dir2));

  EXPECT_FALSE(DeletePath(sub_dir, false));
  EXPECT_TRUE(IsDirectory(sub_dir));
  EXPECT_TRUE(IsDirectory(sub_sub_dir1));

  EXPECT_TRUE(DeletePath(sub_dir, true));
  EXPECT_FALSE(IsDirectory(sub_dir));
  EXPECT_FALSE(IsDirectory(sub_sub_dir1));
}

}  // namespace
}  // namespace files
