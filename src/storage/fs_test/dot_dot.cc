// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/fs_test/misc.h"

namespace fs_test {
namespace {

using DotDotTest = FilesystemTest;

// Test cases of '..' where the path can be canonicalized on the client.
TEST_P(DotDotTest, DotDotClient) {
  ASSERT_EQ(mkdir(GetPath("foo").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("foo/bit").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("foo/bar").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("foo/bar/baz").c_str(), 0755), 0);

  ExpectedDirectoryEntry foo_dir[] = {
      {".", DT_DIR},
      {"bar", DT_DIR},
      {"bit", DT_DIR},
  };

  ExpectedDirectoryEntry bar_dir[] = {
      {".", DT_DIR},
      {"baz", DT_DIR},
  };

  // Test cases of client-side dot-dot when moving between directories.
  DIR* dir = opendir(GetPath("foo/bar/..").c_str());
  ASSERT_NE(dir, nullptr);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, foo_dir));
  ASSERT_EQ(closedir(dir), 0);

  dir = opendir(GetPath("foo/bar/../bit/..//././//").c_str());
  ASSERT_NE(dir, nullptr);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, foo_dir));
  ASSERT_EQ(closedir(dir), 0);

  dir = opendir(GetPath("foo/bar/baz/../../../foo/bar/baz/..").c_str());
  ASSERT_NE(dir, nullptr);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, bar_dir));
  ASSERT_EQ(closedir(dir), 0);

  // Clean up
  ASSERT_EQ(unlink(GetPath("foo/bar/baz").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("foo/bar").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("foo/bit").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("foo").c_str()), 0);
}

// Test cases of '..' where the path cannot be canonicalized on the client.
TEST_P(DotDotTest, DotDotServer) {
  ASSERT_EQ(mkdir(GetPath("foo").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("foo/bar").c_str(), 0755), 0);

  int foo_fd = open(GetPath("foo").c_str(), O_RDONLY | O_DIRECTORY);
  ASSERT_GT(foo_fd, 0);

  // ".." from foo --> Not Supported
  ASSERT_LT(openat(foo_fd, "..", O_RDONLY | O_DIRECTORY), 0);

  // "bar/../.." from foo --> Not supported
  ASSERT_LT(openat(foo_fd, "bar/../..", O_RDONLY | O_DIRECTORY), 0);

  // "../../../../../bar" -->  Not supported
  ASSERT_LT(openat(foo_fd, "../../../../../bar", O_RDONLY | O_DIRECTORY), 0);

  // Try to create a file named '..'
  ASSERT_LT(openat(foo_fd, "..", O_RDWR | O_CREAT), 0);
  ASSERT_LT(openat(foo_fd, ".", O_RDWR | O_CREAT), 0);

  // Try to create a directory named '..'
  ASSERT_LT(mkdirat(foo_fd, "..", 0666), 0);
  ASSERT_LT(mkdirat(foo_fd, ".", 0666), 0);

  // Clean up
  ASSERT_EQ(close(foo_fd), 0);
  ASSERT_EQ(unlink(GetPath("foo/bar").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("foo").c_str()), 0);
}

// Test cases of '..' which operate on multiple paths.
// This is mostly intended to test other pathways for client-side
// cleaning operations.
TEST_P(DotDotTest, DotDotRename) {
  ASSERT_EQ(mkdir(GetPath("foo").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("foo/bit").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("foo/bar").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("foo/bar/baz").c_str(), 0755), 0);

  ExpectedDirectoryEntry foo_dir_bit[] = {
      {".", DT_DIR},
      {"bar", DT_DIR},
      {"bit", DT_DIR},
  };

  ExpectedDirectoryEntry foo_dir_bits[] = {
      {".", DT_DIR},
      {"bar", DT_DIR},
      {"bits", DT_DIR},
  };

  // Check that the source is cleaned
  ASSERT_EQ(rename(GetPath("foo/bar/./../bit/./../bit").c_str(), GetPath("foo/bits").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("foo").c_str(), foo_dir_bits));

  // Check that the destination is cleaned
  ASSERT_EQ(rename(GetPath("foo/bits").c_str(), GetPath("foo/bar/baz/../../././bit").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("foo").c_str(), foo_dir_bit));

  // Check that both are cleaned
  ASSERT_EQ(rename(GetPath("foo/bar/../bit/.").c_str(), GetPath("foo/bar/baz/../../././bits").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("foo").c_str(), foo_dir_bits));

  // Check that both are cleaned (including trailing '/')
  ASSERT_EQ(rename(GetPath("foo/./bar/../bits/").c_str(), GetPath("foo/bar/baz/../../././bit/.//").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("foo").c_str(), foo_dir_bit));

  // Clean up
  ASSERT_EQ(unlink(GetPath("foo/bar/baz").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("foo/bar").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("foo/bit").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("foo").c_str()), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, DotDotTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
