// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fit/defer.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "src/lib/fxl/strings/concatenate.h"

namespace {

// An invalid file descriptor that is not equal to AT_FDCWD.
constexpr int kInvalidFD = -1;
static_assert(kInvalidFD != AT_FDCWD);

TEST(UnistdTest, TruncateWithNegativeLength) {
  const char* filename = "/tmp/truncate-with-negative-length-test";
  fbl::unique_fd fd(open(filename, O_CREAT | O_RDWR, 0666));
  ASSERT_TRUE(fd);
  EXPECT_EQ(-1, ftruncate(fd.get(), -1));
  EXPECT_EQ(EINVAL, errno);
  EXPECT_EQ(-1, ftruncate(fd.get(), std::numeric_limits<off_t>::min()));
  EXPECT_EQ(EINVAL, errno);

  EXPECT_EQ(-1, truncate(filename, -1));
  EXPECT_EQ(EINVAL, errno);
  EXPECT_EQ(-1, truncate(filename, std::numeric_limits<off_t>::min()));
  EXPECT_EQ(EINVAL, errno);
}

TEST(UnistdTest, LinkAt) {
  // Create a temporary directory, store its absolute path and chdir to it.
  char root_abs[] = "/tmp/fdio-linkat.XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(root_abs), "%s", strerror(errno));
  auto cleanup_root =
      fit::defer([&root_abs]() { EXPECT_EQ(0, rmdir(root_abs), "%s", strerror(errno)); });
  char prev_cwd[PATH_MAX];
  ASSERT_NOT_NULL(getcwd(prev_cwd, sizeof(prev_cwd)));
  ASSERT_EQ(0, chdir(root_abs), "%s", strerror(errno));
  auto restore_cwd =
      fit::defer([&prev_cwd]() { EXPECT_EQ(0, chdir(prev_cwd), "%s", strerror(errno)); });

  // Create a subdirectory with a file in it.
  constexpr char dir_name[] = "dir", foo_name[] = "foo", foo_rel[] = "dir/foo";
  ASSERT_EQ(0, mkdir(dir_name, 0777), "%s", strerror(errno));
  auto cleanup_dir =
      fit::defer([&dir_name]() { EXPECT_EQ(0, rmdir(dir_name), "%s", strerror(errno)); });
  ASSERT_TRUE(fbl::unique_fd(creat(foo_rel, 0666)), "%s", strerror(errno));
  auto cleanup_foo =
      fit::defer([&foo_rel]() { EXPECT_EQ(0, unlink(foo_rel), "%s", strerror(errno)); });

  fbl::unique_fd dir_fd(open(dir_name, O_RDONLY | O_DIRECTORY, 0644));
  ASSERT_TRUE(dir_fd, "%s", strerror(errno));

  // Create link using relative paths.
  constexpr char bar_name[] = "bar", bar_rel[] = "dir/bar";
  ASSERT_EQ(0, linkat(dir_fd.get(), foo_name, AT_FDCWD, bar_rel, 0), "%s", strerror(errno));
  ASSERT_EQ(0, unlink(bar_rel), "%s", strerror(errno));
  ASSERT_EQ(0, linkat(AT_FDCWD, foo_rel, dir_fd.get(), bar_name, 0), "%s", strerror(errno));
  ASSERT_EQ(0, unlink(bar_rel), "%s", strerror(errno));

  // Create link using an absolute oldpath (newpath), verifying that olddirfd (newdirfd) is ignored.
  // We also test that an invalid file descriptor is accepted if the corresponding path is absolute.
  const std::string foo_abs = fxl::Concatenate({root_abs, "/", foo_rel});
  const std::string bar_abs = fxl::Concatenate({root_abs, "/", bar_rel});
  for (int olddirfd : {dir_fd.get(), AT_FDCWD, kInvalidFD}) {
    ASSERT_EQ(0, linkat(olddirfd, foo_abs.c_str(), AT_FDCWD, bar_rel, 0), "%s", strerror(errno));
    ASSERT_EQ(0, unlink(bar_rel), "%s", strerror(errno));
  }
  for (int newdirfd : {dir_fd.get(), AT_FDCWD, kInvalidFD}) {
    ASSERT_EQ(0, linkat(AT_FDCWD, foo_rel, newdirfd, bar_abs.c_str(), 0), "%s", strerror(errno));
    ASSERT_EQ(0, unlink(bar_rel), "%s", strerror(errno));
  }

  // Test errors: an invalid file descriptor is not accepted if the corresponding path is relative.
  constexpr char baz_name[] = "baz";
  ASSERT_EQ(-1, linkat(kInvalidFD, foo_rel, AT_FDCWD, baz_name, 0));
  EXPECT_EQ(EBADF, errno, "%s", strerror(errno));
  ASSERT_EQ(-1, linkat(AT_FDCWD, foo_rel, kInvalidFD, baz_name, 0));
  EXPECT_EQ(EBADF, errno, "%s", strerror(errno));
}

TEST(UnistdTest, LinkAtFollow) {
  char root_abs[] = "/tmp/fdio-linkat-follow.XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(root_abs), "%s", strerror(errno));
  auto cleanup_root =
      fit::defer([&root_abs]() { EXPECT_EQ(0, rmdir(root_abs), "%s", strerror(errno)); });

  const std::string file_abs = fxl::Concatenate({root_abs, "/", "file"});
  ASSERT_TRUE(fbl::unique_fd(creat(file_abs.c_str(), 0666)), "%s", strerror(errno));
  auto cleanup_file = fit::defer([&file_abs]() {
    // This must be an ASSERT_* so that errors can be caught by ASSERT_NO_FATAL_FAILURE below.
    ASSERT_EQ(0, unlink(file_abs.c_str()), "%s", strerror(errno));
  });

  // Verify that we can create a hard link to a regular file even if AT_SYMLINK_FOLLOW is set.
  const std::string hard_abs = fxl::Concatenate({root_abs, "/", "hard"});
  ASSERT_EQ(0, linkat(AT_FDCWD, file_abs.c_str(), AT_FDCWD, hard_abs.c_str(), AT_SYMLINK_FOLLOW),
            "%s", strerror(errno));
  ASSERT_EQ(0, unlink(hard_abs.c_str()), "%s", strerror(errno));

  // Create a symlink and test AT_SYMLINK_FOLLOW on it.
  const std::string sym_abs = fxl::Concatenate({root_abs, "/", "sym"});
#ifndef __Fuchsia__
  ASSERT_EQ(0, symlink(file_abs.c_str(), sym_abs.c_str()), "%s", strerror(errno));
  auto cleanup_sym =
      fit::defer([&sym_abs]() { EXPECT_EQ(0, unlink(sym_abs.c_str()), "%s", strerror(errno)); });

  auto expect_file_type_and_unlink = [](const char* path, int expected_file_type) {
    auto unlink_path = fit::defer([path]() { ASSERT_EQ(0, unlink(path), "%s", strerror(errno)); });
    struct stat st;
    ASSERT_EQ(0, lstat(path, &st), "%s", strerror(errno));
    EXPECT_EQ(expected_file_type, st.st_mode & S_IFMT);
  };

  ASSERT_EQ(0, linkat(AT_FDCWD, sym_abs.c_str(), AT_FDCWD, hard_abs.c_str(), 0), "%s",
            strerror(errno));
  ASSERT_NO_FATAL_FAILURE(expect_file_type_and_unlink(hard_abs.c_str(), S_IFLNK));

  ASSERT_EQ(0, linkat(AT_FDCWD, sym_abs.c_str(), AT_FDCWD, hard_abs.c_str(), AT_SYMLINK_FOLLOW),
            "%s", strerror(errno));
  ASSERT_NO_FATAL_FAILURE(expect_file_type_and_unlink(hard_abs.c_str(), S_IFREG));

  // Make our symlink dangling by removing its target.
  ASSERT_NO_FATAL_FAILURE(cleanup_file.call());

  ASSERT_EQ(0, linkat(AT_FDCWD, sym_abs.c_str(), AT_FDCWD, hard_abs.c_str(), 0), "%s",
            strerror(errno));
  ASSERT_NO_FATAL_FAILURE(expect_file_type_and_unlink(hard_abs.c_str(), S_IFLNK));

  ASSERT_EQ(-1, linkat(AT_FDCWD, sym_abs.c_str(), AT_FDCWD, hard_abs.c_str(), AT_SYMLINK_FOLLOW));
  EXPECT_EQ(ENOENT, errno, "%s", strerror(errno));
#else
  // Assert that Fuchsia does not support symlinks yet.
  ASSERT_EQ(-1, symlink(file_abs.c_str(), sym_abs.c_str()));
  ASSERT_EQ(ENOSYS, errno, "%s", strerror(errno));
#endif
}

TEST(UnistdTest, ReadAndWriteWithNegativeOffsets) {
  const char* filename = "/tmp/read-write-with-negative-offsets-test";
  fbl::unique_fd fd(open(filename, O_CREAT | O_RDWR, 0666));
  ASSERT_TRUE(fd);
  ASSERT_EQ(-1, pwrite(fd.get(), "hello", 5, -1));
  ASSERT_EQ(EINVAL, errno, "%s", strerror(errno));
  char buf[5];
  ASSERT_EQ(-1, pwrite(fd.get(), buf, 5, -1));
  ASSERT_EQ(EINVAL, errno, "%s", strerror(errno));
}

}  // namespace
