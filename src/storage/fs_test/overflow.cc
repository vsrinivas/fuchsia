// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <string_view>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using OverflowTest = FilesystemTest;

// Make a 'len' byte long filename (not including null) consisting of the character 'c'.
std::string MakeName(std::string_view mount_path, size_t len, char c) {
  std::string path(mount_path);
  path.append(len, c);
  return path;
}

// Extends 'name' with a string 'len' bytes long, of the character 'c'.
void ExtendName(std::string* name, size_t len, char c) {
  *name += "/";
  name->append(len, c);
}

TEST_P(OverflowTest, NameTooLong) {
  const std::string name_largest = MakeName(fs().mount_path(), NAME_MAX, 'a');
  const std::string name_largest_alt = MakeName(fs().mount_path(), NAME_MAX, 'b');
  const std::string name_too_large = MakeName(fs().mount_path(), NAME_MAX + 1, 'a');

  // Try opening, closing, renaming, and unlinking the largest acceptable name
  int fd = open(name_largest.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(close(fd), 0);
  ASSERT_EQ(rename(name_largest.c_str(), name_largest_alt.c_str()), 0);
  ASSERT_EQ(rename(name_largest_alt.c_str(), name_largest.c_str()), 0);

  ASSERT_EQ(rename(name_largest.c_str(), name_too_large.c_str()), -1);
  ASSERT_EQ(rename(name_too_large.c_str(), name_largest.c_str()), -1);
  ASSERT_EQ(unlink(name_largest.c_str()), 0);

  // Try it with a directory too
  ASSERT_EQ(mkdir(name_largest.c_str(), 0755), 0);
  ASSERT_EQ(rename(name_largest.c_str(), name_largest_alt.c_str()), 0);
  ASSERT_EQ(rename(name_largest_alt.c_str(), name_largest.c_str()), 0);

  ASSERT_EQ(rename(name_largest.c_str(), name_too_large.c_str()), -1);
  ASSERT_EQ(rename(name_too_large.c_str(), name_largest.c_str()), -1);
  ASSERT_EQ(unlink(name_largest.c_str()), 0);

  // Try opening an unacceptably large name
  ASSERT_EQ(open(name_too_large.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644), -1);
  // Try it with a directory too
  ASSERT_EQ(mkdir(name_too_large.c_str(), 0755), -1);
}

TEST_P(OverflowTest, PathTooLong) {
  int depth = 0;

  // Create an initial directory
  std::string name = MakeName(fs().mount_path(), NAME_MAX, 'a');
  ASSERT_EQ(mkdir(name.c_str(), 0755), 0);
  depth++;
  // Create child directories until we hit PATH_MAX
  while (true) {
    ExtendName(&name, NAME_MAX, 'a');
    int r = mkdir(name.c_str(), 0755);
    if (r < 0) {
      ASSERT_EQ(errno, ENAMETOOLONG);
      break;
    }
    depth++;
  }

  // Remove all child directories
  while (depth != 0) {
    size_t last_slash = name.rfind('/');
    assert(last_slash != std::string::npos);
    name.resize(last_slash);
    ASSERT_EQ(unlink(name.c_str()), 0);
    depth--;
  }
}

TEST_P(OverflowTest, OutOfRangeTruncateAndSeekFails) {
  const std::string filename = GetPath("file");
  int fd = open(filename.c_str(), O_CREAT | O_RDWR | O_EXCL, 0644);
  ASSERT_GT(fd, 0);

  // TODO(smklein): Test extremely large reads/writes when remoteio can handle them without
  // crashing
  /*
  char buf[4096];
  ASSERT_EQ(write(fd, buf, SIZE_MAX - 1), -1);
  ASSERT_EQ(write(fd, buf, SIZE_MAX), -1);

  ASSERT_EQ(read(fd, buf, SIZE_MAX - 1), -1);
  ASSERT_EQ(read(fd, buf, SIZE_MAX), -1);
  */

  ASSERT_EQ(ftruncate(fd, INT_MIN), -1);
  ASSERT_EQ(ftruncate(fd, -1), -1);
  ASSERT_EQ(ftruncate(fd, SIZE_MAX - 1), -1);
  ASSERT_EQ(ftruncate(fd, SIZE_MAX), -1);

  ASSERT_EQ(lseek(fd, INT_MIN, SEEK_SET), -1);
  ASSERT_EQ(lseek(fd, -1, SEEK_SET), -1);
  ASSERT_EQ(lseek(fd, SIZE_MAX - 1, SEEK_SET), -1);
  ASSERT_EQ(lseek(fd, SIZE_MAX, SEEK_SET), -1);
  ASSERT_EQ(close(fd), 0);
  ASSERT_EQ(unlink(filename.c_str()), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, OverflowTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
