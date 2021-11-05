// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/file_util.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/scoped_temp_file.h"

namespace zxdb {

TEST(FileUtil, ExtractLastFileComponent) {
  EXPECT_EQ("", ExtractLastFileComponent(""));
  EXPECT_EQ("", ExtractLastFileComponent("foo/"));
  EXPECT_EQ("foo.cpp", ExtractLastFileComponent("foo.cpp"));
  EXPECT_EQ("foo.cpp", ExtractLastFileComponent("bar/foo.cpp"));
  EXPECT_EQ("foo.cpp", ExtractLastFileComponent("baz/bar/foo.cpp"));
}

TEST(FileUtil, IsPathAbsolute) {
  EXPECT_FALSE(IsPathAbsolute(""));
  EXPECT_TRUE(IsPathAbsolute("/"));
  EXPECT_TRUE(IsPathAbsolute("/foo/bar"));
  EXPECT_FALSE(IsPathAbsolute("foo/bar"));
  EXPECT_FALSE(IsPathAbsolute("./foo/bar"));
}

TEST(FileUtil, PathContainsFromRight) {
  EXPECT_TRUE(PathContainsFromRight("", ""));
  EXPECT_TRUE(PathContainsFromRight("foo.cc", "foo.cc"));
  EXPECT_TRUE(PathContainsFromRight("/foo.cc", "foo.cc"));
  EXPECT_TRUE(PathContainsFromRight("bar/foo.cc", "foo.cc"));
  EXPECT_TRUE(PathContainsFromRight("bar/foo.cc", "bar/foo.cc"));

  EXPECT_FALSE(PathContainsFromRight("bar/foo.cc", "FOO.CC"));
  EXPECT_FALSE(PathContainsFromRight("bar/foo.cc", "o.cc"));
}

TEST(FileUtil, CatPathComponents) {
  EXPECT_EQ("", CatPathComponents("", ""));
  EXPECT_EQ("a", CatPathComponents("", "a"));
  EXPECT_EQ("a", CatPathComponents("a", ""));
  EXPECT_EQ("a/b", CatPathComponents("a", "b"));
  EXPECT_EQ("a/b", CatPathComponents("a/", "b"));
  EXPECT_EQ("a/b/", CatPathComponents("a/", "b/"));
}

TEST(FileUtil, NormalizePath) {
  EXPECT_EQ("", NormalizePath(""));
  EXPECT_EQ("foo/bar.txt", NormalizePath("foo/bar.txt"));
  EXPECT_EQ(".", NormalizePath("."));
  EXPECT_EQ("foo/bar", NormalizePath("foo//bar"));
  EXPECT_EQ("/foo", NormalizePath("//foo"));
  EXPECT_EQ("bar", NormalizePath("foo/..//bar"));
  EXPECT_EQ("../bar", NormalizePath("foo/../../bar"));
  EXPECT_EQ("/foo", NormalizePath("/../foo"));  // Don't go above the root dir.
  EXPECT_EQ("/foo", NormalizePath("/../foo"));  // Don't go above the root dir.
  EXPECT_EQ("../foo", NormalizePath("../foo"));
  EXPECT_EQ("..", NormalizePath(".."));
  EXPECT_EQ(".", NormalizePath("./././."));
  EXPECT_EQ("../../..", NormalizePath("../../.."));

  // The implementation of this is std::filesystem which isn't consistent here about whether
  // trailing slashes are preserved. It would be nice if the "../" case preserved the trailing
  // slash for consistency, but this behavior should be fine for our needs.
  EXPECT_EQ("..", NormalizePath("../"));
  EXPECT_EQ("/foo/bar/", NormalizePath("/foo/bar/"));
}

TEST(FileUtil, GetFileModificationTime) {
  ScopedTempFile temp_file;

  time_t modification_time = GetFileModificationTime(temp_file.name());
  time_t now = time(nullptr);

  // EXPECT_NEAR accepts double rather than int.
  EXPECT_GT(modification_time, now - 10);
  EXPECT_LT(modification_time, now + 10);
}

}  // namespace zxdb
