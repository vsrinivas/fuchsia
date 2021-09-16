// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <optional>
#include <random>
#include <vector>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/fs_test/misc.h"

namespace fs_test {
namespace {

using HardLinkTest = FilesystemTest;

void CheckLinkCount(const std::string& path, unsigned count) {
  struct stat s;
  ASSERT_EQ(stat(path.c_str(), &s), 0);
  ASSERT_EQ(s.st_nlink, count);
}

TEST_P(HardLinkTest, Basic) {
  const std::string old_path = GetPath("a");
  const std::string new_path = GetPath("b");

  // Make a file, fill it with content
  int fd = open(old_path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  uint8_t buf[100];
  for (size_t i = 0; i < sizeof(buf); i++) {
    buf[i] = (uint8_t)rand();
  }
  ASSERT_EQ(write(fd, buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
  ASSERT_NO_FATAL_FAILURE(CheckFileContents(fd, buf));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(old_path, 1));

  ASSERT_EQ(link(old_path.c_str(), new_path.c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(old_path, 2));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(new_path, 2));

  // Confirm that both the old link and the new links exist
  int fd2 = open(new_path.c_str(), O_RDONLY, 0644);
  ASSERT_GT(fd2, 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileContents(fd2, buf));
  ASSERT_NO_FATAL_FAILURE(CheckFileContents(fd, buf));

  // Remove the old link
  ASSERT_EQ(close(fd), 0);
  ASSERT_EQ(close(fd2), 0);
  ASSERT_EQ(unlink(old_path.c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(new_path, 1));

  // Open the link by its new name, and verify that the contents have
  // not been altered by the removal of the old link.
  fd = open(new_path.c_str(), O_RDONLY, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileContents(fd, buf));

  ASSERT_EQ(close(fd), 0);
  ASSERT_EQ(unlink(new_path.c_str()), 0);
}

TEST_P(HardLinkTest, test_link_count_dirs) {
  ASSERT_EQ(mkdir(GetPath("dira").c_str(), 0755), 0);
  // New directories should have two links:
  // Parent --> newdir
  // newdir ('.') --> newdir
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira"), 2));

  // Adding a file won't change the parent link count...
  int fd = open(GetPath("dira/file").c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(close(fd), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira"), 2));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira/file"), 1));

  // But adding a directory WILL change the parent link count.
  ASSERT_EQ(mkdir(GetPath("dira/dirb").c_str(), 0755), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira/dirb"), 2));

  // Test that adding "depth" increases the dir count as we expect.
  ASSERT_EQ(mkdir(GetPath("dira/dirb/dirc").c_str(), 0755), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira/dirb"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira/dirb/dirc"), 2));

  // Demonstrate that unwinding also reduces the link count.
  ASSERT_EQ(unlink(GetPath("dira/dirb/dirc").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira/dirb"), 2));

  ASSERT_EQ(unlink(GetPath("dira/dirb").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira"), 2));

  // Test that adding "width" increases the dir count too.
  ASSERT_EQ(mkdir(GetPath("dira/dirb").c_str(), 0755), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira/dirb"), 2));

  ASSERT_EQ(mkdir(GetPath("dira/dirc").c_str(), 0755), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira"), 4));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira/dirb"), 2));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira/dirc"), 2));

  // Demonstrate that unwinding also reduces the link count.
  ASSERT_EQ(unlink(GetPath("dira/dirc").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira/dirb"), 2));

  ASSERT_EQ(unlink(GetPath("dira/dirb").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira"), 2));

  ASSERT_EQ(unlink(GetPath("dira/file").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("dira").c_str()), 0);
}

TEST_P(HardLinkTest, CorrectLinkCountAfterRename) {
  // Check that link count does not change with simple rename
  ASSERT_EQ(mkdir(GetPath("dir").c_str(), 0755), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir"), 2));
  ASSERT_EQ(rename(GetPath("dir").c_str(), GetPath("dir_parent").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent"), 2));

  // Set up parent directory with child directories
  ASSERT_EQ(mkdir(GetPath("dir_parent/dir_child_a").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("dir_parent/dir_child_b").c_str(), 0755), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent"), 4));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent/dir_child_a"), 2));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent/dir_child_b"), 2));

  // Rename a child directory out of its parent directory
  ASSERT_EQ(rename(GetPath("dir_parent/dir_child_b").c_str(), GetPath("dir_parent_alt").c_str()),
            0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent/dir_child_a"), 2));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt"), 2));

  // Rename a parent directory into another directory
  ASSERT_EQ(
      rename(GetPath("dir_parent").c_str(), GetPath("dir_parent_alt/dir_semi_parent").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt/dir_semi_parent"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt/dir_semi_parent/dir_child_a"), 2));

  // Rename a directory on top of an empty directory
  ASSERT_EQ(mkdir(GetPath("dir_child").c_str(), 0755), 0);
  ASSERT_EQ(rename(GetPath("dir_child").c_str(),
                   GetPath("dir_parent_alt/dir_semi_parent/dir_child_a").c_str()),
            0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt/dir_semi_parent"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt/dir_semi_parent/dir_child_a"), 2));

  // Rename a directory on top of an empty directory from a non-root directory
  ASSERT_EQ(mkdir(GetPath("dir").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("dir/dir_child").c_str(), 0755), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir/dir_child"), 2));
  ASSERT_EQ(rename(GetPath("dir/dir_child").c_str(),
                   GetPath("dir_parent_alt/dir_semi_parent/dir_child_a").c_str()),
            0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir"), 2));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt/dir_semi_parent"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt/dir_semi_parent/dir_child_a"), 2));

  // Rename a file on top of a file from a non-root directory
  ASSERT_EQ(unlink(GetPath("dir_parent_alt/dir_semi_parent/dir_child_a").c_str()), 0);
  int fd = open(GetPath("dir/dir_child").c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir"), 2));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir/dir_child"), 1));
  int fd2 = open(GetPath("dir_parent_alt/dir_semi_parent/dir_child_a").c_str(),
                 O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd2, 0);
  ASSERT_EQ(rename(GetPath("dir/dir_child").c_str(),
                   GetPath("dir_parent_alt/dir_semi_parent/dir_child_a").c_str()),
            0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir"), 2));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt/dir_semi_parent"), 2));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt/dir_semi_parent/dir_child_a"), 1));
  ASSERT_EQ(close(fd), 0);
  ASSERT_EQ(close(fd2), 0);

  // Clean up
  ASSERT_EQ(unlink(GetPath("dir_parent_alt/dir_semi_parent/dir_child_a").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt"), 3));
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt/dir_semi_parent"), 2));
  ASSERT_EQ(unlink(GetPath("dir_parent_alt/dir_semi_parent").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dir_parent_alt"), 2));
  ASSERT_EQ(unlink(GetPath("dir_parent_alt").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("dir").c_str()), 0);
}

TEST_P(HardLinkTest, AcrossDirectories) {
  ASSERT_EQ(mkdir(GetPath("dira").c_str(), 0755), 0);
  // New directories should have two links:
  // Parent --> newdir
  // newdir ('.') --> newdir
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dira"), 2));

  ASSERT_EQ(mkdir(GetPath("dirb").c_str(), 0755), 0);
  ASSERT_NO_FATAL_FAILURE(CheckLinkCount(GetPath("dirb"), 2));

  const std::string old_path = GetPath("dira/a");
  const std::string new_path = GetPath("dirb/b");

  // Make a file, fill it with content
  int fd = open(old_path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  uint8_t buf[100];
  for (size_t i = 0; i < sizeof(buf); i++) {
    buf[i] = (uint8_t)rand();
  }
  ASSERT_EQ(write(fd, buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
  ASSERT_NO_FATAL_FAILURE(CheckFileContents(fd, buf));

  ASSERT_EQ(link(old_path.c_str(), new_path.c_str()), 0);

  // Confirm that both the old link and the new links exist
  int fd2 = open(new_path.c_str(), O_RDWR, 0644);
  ASSERT_GT(fd2, 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileContents(fd2, buf));
  ASSERT_NO_FATAL_FAILURE(CheckFileContents(fd, buf));

  // Remove the old link
  ASSERT_EQ(close(fd), 0);
  ASSERT_EQ(close(fd2), 0);
  ASSERT_EQ(unlink(old_path.c_str()), 0);

  // Open the link by its new name
  fd = open(new_path.c_str(), O_RDWR, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileContents(fd, buf));

  ASSERT_EQ(close(fd), 0);
  ASSERT_EQ(unlink(new_path.c_str()), 0);
  ASSERT_EQ(unlink(GetPath("dira").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("dirb").c_str()), 0);
}

TEST_P(HardLinkTest, Errors) {
  const std::string dir_path = GetPath("dir");
  const std::string old_path = GetPath("a");
  const std::string new_path = GetPath("b");
  const std::string new_path_dir = GetPath("b/");

  // We should not be able to create hard links to directories
  ASSERT_EQ(mkdir(dir_path.c_str(), 0755), 0);
  ASSERT_EQ(link(dir_path.c_str(), new_path.c_str()), -1);
  ASSERT_EQ(unlink(dir_path.c_str()), 0);

  // We should not be able to create hard links to non-existent files
  ASSERT_EQ(link(old_path.c_str(), new_path.c_str()), -1);
  ASSERT_EQ(errno, ENOENT);

  int fd = open(old_path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(close(fd), 0);

  // We should not be able to link to or from . or ..
  ASSERT_EQ(link(old_path.c_str(), GetPath(".").c_str()), -1);
  ASSERT_EQ(link(old_path.c_str(), GetPath("..").c_str()), -1);
  ASSERT_EQ(link(GetPath(".").c_str(), new_path.c_str()), -1);
  ASSERT_EQ(link(GetPath("..").c_str(), new_path.c_str()), -1);

  // We should not be able to link a file to itself
  ASSERT_EQ(link(old_path.c_str(), old_path.c_str()), -1);
  ASSERT_EQ(errno, EEXIST);

  // We should not be able to link a file to a path that implies it must be a directory
  ASSERT_EQ(link(old_path.c_str(), new_path_dir.c_str()), -1);

  // After linking, we shouldn't be able to link again
  ASSERT_EQ(link(old_path.c_str(), new_path.c_str()), 0);
  ASSERT_EQ(link(old_path.c_str(), new_path.c_str()), -1);
  ASSERT_EQ(errno, EEXIST);
  // In either order
  ASSERT_EQ(link(new_path.c_str(), old_path.c_str()), -1);
  ASSERT_EQ(errno, EEXIST);

  ASSERT_EQ(unlink(new_path.c_str()), 0);
  ASSERT_EQ(unlink(old_path.c_str()), 0);
}

TEST_P(HardLinkTest, UnlinkRace) {
  const std::string file = GetPath("a");
  const char* filename = file.c_str();

  std::random_device random;
  std::uniform_int_distribution distribution(0, 1000);

  for (int i = 0; i < 100; ++i) {
    {
      fbl::unique_fd fd(open(filename, O_RDWR | O_CREAT | O_EXCL, 0644));
      ASSERT_TRUE(fd);
      EXPECT_EQ(write(fd.get(), "hello", 5), 5);
    }

    std::thread thread([&] {
      const std::string file2 = GetPath("b");
      const char* filename2 = file2.c_str();
      if (link(filename, filename2) == 0) {
        // The link succeeded
        fbl::unique_fd fd(open(filename2, O_RDONLY));
        ASSERT_TRUE(fd);
        char buf[5];
        EXPECT_EQ(read(fd.get(), buf, 5), 5);
        EXPECT_EQ(memcmp(buf, "hello", 5), 0);
        EXPECT_EQ(unlink(filename2), 0);
      } else {
        ASSERT_EQ(errno, ENOENT);
      }
    });
    int time = distribution(random);
    usleep(time);
    EXPECT_EQ(unlink(filename), 0) << "errno: " << errno;
    thread.join();
  }
}

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, HardLinkTest,
    testing::ValuesIn(MapAndFilterAllTestFilesystems(
        [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
          if (options.filesystem->GetTraits().supports_hard_links) {
            return options;
          } else {
            return std::nullopt;
          }
        })),
    testing::PrintToStringParamName());

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(HardLinkTest);

}  // namespace
}  // namespace fs_test
