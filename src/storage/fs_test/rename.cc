// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/errors.h>

#include <string_view>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/fs_test/misc.h"

namespace fs_test {
namespace {

using RenameTest = FilesystemTest;

namespace fio = ::llcpp::fuchsia::io;

TEST_P(RenameTest, Basic) {
  // Cannot rename when src does not exist
  ASSERT_EQ(rename(GetPath("alpha").c_str(), GetPath("bravo").c_str()), -1);

  // Renaming to self is fine
  ASSERT_EQ(mkdir(GetPath("alpha").c_str(), 0755), 0);
  ASSERT_EQ(rename(GetPath("alpha").c_str(), GetPath("alpha").c_str()), 0);
  ASSERT_EQ(rename(GetPath("alpha/.").c_str(), GetPath("alpha/.").c_str()), 0);
  ASSERT_EQ(rename(GetPath("alpha/").c_str(), GetPath("alpha").c_str()), 0);
  ASSERT_EQ(rename(GetPath("alpha").c_str(), GetPath("alpha/").c_str()), 0);
  ASSERT_EQ(rename(GetPath("alpha/").c_str(), GetPath("alpha/").c_str()), 0);
  ASSERT_EQ(rename(GetPath("alpha/./../alpha").c_str(), GetPath("alpha/./../alpha").c_str()), 0);

  // Cannot rename dir to file
  int fd = open(GetPath("bravo").c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(close(fd), 0);
  ASSERT_EQ(rename(GetPath("alpha").c_str(), GetPath("bravo").c_str()), -1);
  ASSERT_EQ(unlink(GetPath("bravo").c_str()), 0);

  // Rename dir (dst does not exist)
  ASSERT_EQ(rename(GetPath("alpha").c_str(), GetPath("bravo").c_str()), 0);
  ASSERT_EQ(mkdir(GetPath("alpha").c_str(), 0755), 0);
  // Rename dir (dst does exist)
  ASSERT_EQ(rename(GetPath("bravo").c_str(), GetPath("alpha").c_str()), 0);

  // Rename file (dst does not exist)
  fd = open(GetPath("alpha/charlie").c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(rename(GetPath("alpha/charlie").c_str(), GetPath("alpha/delta").c_str()), 0);
  // File rename to self
  ASSERT_EQ(rename(GetPath("alpha/delta").c_str(), GetPath("alpha/delta").c_str()), 0);
  // Not permitted with trailing '/'
  ASSERT_EQ(rename(GetPath("alpha/delta").c_str(), GetPath("alpha/delta/").c_str()), -1);
  ASSERT_EQ(rename(GetPath("alpha/delta/").c_str(), GetPath("alpha/delta").c_str()), -1);
  ASSERT_EQ(rename(GetPath("alpha/delta/").c_str(), GetPath("alpha/delta/").c_str()), -1);
  ASSERT_EQ(close(fd), 0);

  // Rename file (dst does not exist)
  fd = open(GetPath("alpha/charlie").c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(rename(GetPath("alpha/delta").c_str(), GetPath("alpha/charlie").c_str()), 0);
  ASSERT_EQ(close(fd), 0);

  // Rename to different directory
  ASSERT_EQ(mkdir(GetPath("bravo").c_str(), 0755), 0);
  ASSERT_EQ(rename(GetPath("alpha/charlie").c_str(), GetPath("charlie").c_str()), 0);
  ASSERT_EQ(rename(GetPath("charlie").c_str(), GetPath("alpha/charlie").c_str()), 0);
  ASSERT_EQ(rename(GetPath("bravo").c_str(), GetPath("alpha/bravo").c_str()), 0);
  ASSERT_EQ(rename(GetPath("alpha/charlie").c_str(), GetPath("alpha/bravo/charlie").c_str()), 0);

  // Cannot rename directory to subdirectory of itself
  ASSERT_EQ(rename(GetPath("alpha").c_str(), GetPath("alpha/bravo").c_str()), -1);
  ASSERT_EQ(rename(GetPath("alpha").c_str(), GetPath("alpha/bravo/charlie").c_str()), -1);
  ASSERT_EQ(rename(GetPath("alpha").c_str(), GetPath("alpha/bravo/charlie/delta").c_str()), -1);
  ASSERT_EQ(rename(GetPath("alpha").c_str(), GetPath("alpha/delta").c_str()), -1);
  ASSERT_EQ(rename(GetPath("alpha/bravo").c_str(), GetPath("alpha/bravo/charlie").c_str()), -1);
  ASSERT_EQ(rename(GetPath("alpha/bravo").c_str(), GetPath("alpha/bravo/charlie/delta").c_str()),
            -1);
  // Cannot rename to non-empty directory
  ASSERT_EQ(rename(GetPath("alpha/bravo/charlie").c_str(), GetPath("alpha/bravo").c_str()), -1);
  ASSERT_EQ(rename(GetPath("alpha/bravo/charlie").c_str(), GetPath("alpha").c_str()), -1);
  ASSERT_EQ(rename(GetPath("alpha/bravo").c_str(), GetPath("alpha").c_str()), -1);

  // Clean up
  ASSERT_EQ(unlink(GetPath("alpha/bravo/charlie").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("alpha/bravo").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("alpha").c_str()), 0);
}

TEST_P(RenameTest, Children) {
  ASSERT_EQ(mkdir(GetPath("dir_before_move").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("dir_before_move/dir1").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("dir_before_move/dir2").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("dir_before_move/dir2/subdir").c_str(), 0755), 0);
  int fd = open(GetPath("dir_before_move/file").c_str(), O_RDWR | O_CREAT, 0644);
  ASSERT_GT(fd, 0);

  static constexpr uint8_t file_contents_array[] = "This should be in the file";
  constexpr std::basic_string_view file_contents = file_contents_array;
  ASSERT_EQ(write(fd, file_contents.data(), file_contents.size()),
            static_cast<ssize_t>(file_contents.size()));

  ASSERT_EQ(rename(GetPath("dir_before_move").c_str(), GetPath("dir").c_str()), 0);

  // Check that the directory layout has persisted across rename
  ExpectedDirectoryEntry dir_contents[] = {
      {".", DT_DIR},
      {"dir1", DT_DIR},
      {"dir2", DT_DIR},
      {"file", DT_REG},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("dir").c_str(), dir_contents));
  ExpectedDirectoryEntry dir2_contents[] = {
      {".", DT_DIR},
      {"subdir", DT_DIR},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("dir/dir2").c_str(), dir2_contents));

  // Check the our file data has lasted (without re-opening)
  ASSERT_NO_FATAL_FAILURE(CheckFileContents(fd, file_contents));

  // Check the our file data has lasted (with re-opening)
  ASSERT_EQ(close(fd), 0);
  fd = open(GetPath("dir/file").c_str(), O_RDONLY, 06444);
  ASSERT_GT(fd, 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileContents(fd, file_contents));
  ASSERT_EQ(close(fd), 0);

  // Clean up
  ASSERT_EQ(unlink(GetPath("dir/dir1").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("dir/dir2/subdir").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("dir/dir2").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("dir/file").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("dir").c_str()), 0);
}

TEST_P(RenameTest, AbsoluteRelative) {
  char cwd[PATH_MAX];
  ASSERT_NE(getcwd(cwd, sizeof(cwd)), nullptr);

  // Change the cwd to a known directory
  ASSERT_EQ(mkdir(GetPath("working_dir").c_str(), 0755), 0);
  DIR* dir = opendir(GetPath("working_dir").c_str());
  ASSERT_NE(dir, nullptr);
  ASSERT_EQ(chdir(GetPath("working_dir").c_str()), 0);

  // Make a "foo" directory in the cwd
  int fd = dirfd(dir);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(mkdirat(fd, "foo", 0755), 0);
  ExpectedDirectoryEntry dir_contents_foo[] = {
      {".", DT_DIR},
      {"foo", DT_DIR},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, dir_contents_foo));

  // Rename "foo" to "bar" using mixed paths
  ASSERT_EQ(rename(GetPath("working_dir/foo").c_str(), "bar"), 0);
  ExpectedDirectoryEntry dir_contents_bar[] = {
      {".", DT_DIR},
      {"bar", DT_DIR},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, dir_contents_bar));

  // Rename "bar" back to "foo" using mixed paths in the other direction
  ASSERT_EQ(rename("bar", GetPath("working_dir/foo").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, dir_contents_foo));

  ASSERT_EQ(rmdir(GetPath("working_dir/foo").c_str()), 0);

  // Change the cwd back to the original, whatever it was before
  // this test started
  ASSERT_EQ(chdir(cwd), 0) << "Could not return to original cwd";

  ASSERT_EQ(rmdir(GetPath("working_dir").c_str()), 0);
  ASSERT_EQ(closedir(dir), 0);
}

TEST_P(RenameTest, At) {
  ASSERT_EQ(mkdir(GetPath("foo").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("foo/baz").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("bar").c_str(), 0755), 0);

  // Normal case of renameat, from one directory to another
  int foofd = open(GetPath("foo").c_str(), O_RDONLY | O_DIRECTORY, 0644);
  ASSERT_GT(foofd, 0);
  int barfd = open(GetPath("bar").c_str(), O_RDONLY | O_DIRECTORY, 0644);
  ASSERT_GT(barfd, 0);

  ASSERT_EQ(renameat(foofd, "baz", barfd, "zab"), 0);

  ExpectedDirectoryEntry empty_contents[] = {
      {".", DT_DIR},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("foo").c_str(), empty_contents));
  ExpectedDirectoryEntry contains_zab[] = {
      {".", DT_DIR},
      {"zab", DT_DIR},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("bar").c_str(), contains_zab));

  // Alternate case of renameat, where an absolute path ignores
  // the file descriptor.
  //
  // Here, barfd is used (in the first argument) but ignored (in the second argument).
  ASSERT_EQ(renameat(barfd, "zab", barfd, GetPath("foo/baz").c_str()), 0);
  ExpectedDirectoryEntry contains_baz[] = {
      {".", DT_DIR},
      {"baz", DT_DIR},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("foo").c_str(), contains_baz));
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("bar").c_str(), empty_contents));

  // The 'absolute-path-ignores-fd' case should also work with invalid fds.
  ASSERT_EQ(renameat(-1, GetPath("foo/baz").c_str(), -1, GetPath("bar/baz").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("foo").c_str(), empty_contents));
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("bar").c_str(), contains_baz));

  // However, relative paths should not be allowed with invalid fds.
  ASSERT_EQ(renameat(-1, "baz", foofd, "baz"), -1);
  ASSERT_EQ(errno, EBADF);

  // Additionally, we shouldn't be able to renameat to a file.
  int fd = openat(barfd, "filename", O_CREAT | O_RDWR | O_EXCL);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(renameat(foofd, "baz", fd, "baz"), -1);
  // NOTE: not checking for "ENOTDIR", since ENOTSUPPORTED might be returned instead.

  // Clean up
  ASSERT_EQ(close(fd), 0);
  ASSERT_EQ(unlink(GetPath("bar/filename").c_str()), 0);
  ASSERT_EQ(rmdir(GetPath("bar/baz").c_str()), 0);
  ASSERT_EQ(close(foofd), 0);
  ASSERT_EQ(close(barfd), 0);
  ASSERT_EQ(rmdir(GetPath("foo").c_str()), 0);
  ASSERT_EQ(rmdir(GetPath("bar").c_str()), 0);
}

TEST_P(RenameTest, RenameDirOverFileFails) {
  std::string src_dir = GetPath("a/b/");
  std::string dst = GetPath("a/c");

  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(src_dir.c_str(), 0755), 0);

  // Renaming over a file fails.
  int fd = open(dst.c_str(), O_CREAT | O_RDWR);
  ASSERT_GT(fd, 0);
  close(fd);

  ASSERT_EQ(rename(src_dir.c_str(), dst.c_str()), -1);
  ASSERT_EQ(errno, ENOTDIR);
  // ... and check with no trailing slash
  ASSERT_EQ(rename(GetPath("a/b").c_str(), dst.c_str()), -1);
  ASSERT_EQ(errno, ENOTDIR);

  ASSERT_EQ(unlink(dst.c_str()), 0);
}

TEST_P(RenameTest, RenameDirOverEmptyDirSucceeds) {
  std::string src_dir = GetPath("a/b/");
  std::string dst = GetPath("a/c");

  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(src_dir.c_str(), 0755), 0);

  ASSERT_EQ(mkdir(dst.c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("a/b/test").c_str(), 0755), 0);

  ASSERT_EQ(rename(src_dir.c_str(), dst.c_str()), 0);

  ExpectedDirectoryEntry contents[] = {
      {".", DT_DIR},
      {"test", DT_DIR},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dst.c_str(), contents));

  ASSERT_EQ(rmdir(GetPath("a/c/test").c_str()), 0);
  ASSERT_EQ(rmdir(dst.c_str()), 0);
}

// If we try and rename a/b/ when b is a file, the rename should fail.
TEST_P(RenameTest, RenameFileTrailingSlashFails) {
  std::string src_dir = GetPath("a/b/");
  std::string dst = GetPath("a/c");
  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), 0);
  int fd = open(GetPath("a/b").c_str(), O_CREAT | O_RDWR);
  ASSERT_GT(fd, 0);
  close(fd);

  ASSERT_EQ(rename(src_dir.c_str(), dst.c_str()), -1);
  ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(RenameTest, RenameDirOverNonEmptyDirFails) {
  std::string b_dir = GetPath("a/b/");
  std::string c_dir = GetPath("a/c/");
  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(b_dir.c_str(), 0755), 0);
  ASSERT_EQ(mkdir(c_dir.c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("a/b/d").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("a/c/e").c_str(), 0755), 0);

  ASSERT_EQ(rename(b_dir.c_str(), c_dir.c_str()), -1);
  ASSERT_EQ(errno, ENOTEMPTY);
}

TEST_P(RenameTest, RenameFileOverDirFails) {
  std::string src = GetPath("a/b");
  std::string dst = GetPath("a/c/");
  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(dst.c_str(), 0755), 0);

  int fd = open(src.c_str(), O_CREAT | O_RDWR);
  ASSERT_GT(fd, 0);
  close(fd);

  ASSERT_EQ(rename(src.c_str(), dst.c_str()), -1);
  ASSERT_EQ(errno, ENOTDIR);
  ASSERT_EQ(rename(src.c_str(), GetPath("a/c").c_str()), -1);
  ASSERT_EQ(errno, EISDIR);
}

TEST_P(RenameTest, RenameFileOverNonexistantDirPathFails) {
  std::string src = GetPath("a/b");
  std::string dst = GetPath("a/c/");
  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), 0);
  int fd = open(src.c_str(), O_CREAT | O_RDWR);
  ASSERT_GT(fd, 0);
  close(fd);

  ASSERT_EQ(rename(src.c_str(), dst.c_str()), -1);
  ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(RenameTest, RenameFileOverNonexistantFilePathSucceeds) {
  std::string src = GetPath("a/b");
  std::string dst = GetPath("a/c");
  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), 0);
  int fd = open(src.c_str(), O_CREAT | O_RDWR);
  ASSERT_GT(fd, 0);
  close(fd);

  ASSERT_EQ(rename(src.c_str(), dst.c_str()), 0);
}

// Rename using the raw FIDL interface.
TEST_P(RenameTest, Raw) {
  ASSERT_EQ(mkdir(GetPath("alpha").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("alpha/bravo").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("alpha/bravo/charlie").c_str(), 0755), 0);

  fbl::unique_fd fd(open(GetPath("alpha").c_str(), O_RDONLY | O_DIRECTORY, 0644));
  ASSERT_TRUE(fd);
  fdio_cpp::FdioCaller caller(std::move(fd));

  auto token_result = fio::Directory::Call::GetToken(caller.channel());
  ASSERT_EQ(token_result.status(), ZX_OK);
  ASSERT_EQ(token_result.Unwrap()->s, ZX_OK);

  // Pass a path, instead of a name, to rename.
  // Observe that paths are rejected.
  constexpr char src[] = "bravo/charlie";
  constexpr char dst[] = "bravo/delta";
  auto rename_result =
      fio::Directory::Call::Rename(caller.channel(), fidl::StringView(src),
                                   std::move(token_result.Unwrap()->token), fidl::StringView(dst));
  ASSERT_EQ(rename_result.status(), ZX_OK);
  ASSERT_EQ(rename_result.Unwrap()->s, ZX_ERR_INVALID_ARGS);

  // Clean up
  ASSERT_EQ(unlink(GetPath("alpha/bravo/charlie").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("alpha/bravo").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("alpha").c_str()), 0);
}

TEST_P(RenameTest, RenameDirIntoRootSuceeds) {
  ASSERT_EQ(mkdir(GetPath("alpha").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("alpha/bravo").c_str(), 0755), 0);
  EXPECT_EQ(rename(GetPath("alpha/bravo").c_str(), GetPath("bravo").c_str()), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, RenameTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
