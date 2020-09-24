// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/fs_test/misc.h"

namespace fs_test {
namespace {

using DirectoryTest = FilesystemTest;

TEST_P(DirectoryTest, DirectoryFilenameMax) {
  // TODO(smklein): This value may be filesystem-specific. Plumb it through
  // from the test driver.
  constexpr int max_file_len = 255;

  // Unless the max_file_len is approaching half of PATH_MAX,
  // this shouldn't be an issue.
  static_assert((2 /* '::' */ + (max_file_len + 1) + 1 /* slash */ + max_file_len) < PATH_MAX);
  // Large components should not crash vfs
  std::string path =
      GetPath(fbl::StringPrintf("%0*d/%0*d", max_file_len + 1, 0xBEEF, max_file_len, 0xBEEF));
  ASSERT_EQ(open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644), -1);
  ASSERT_EQ(errno, ENAMETOOLONG);

  // Largest possible file length
  path = GetPath(fbl::StringPrintf("%0*d", max_file_len, 0x1337));
  fbl::unique_fd fd(open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(path.c_str()), 0);

  // Slightly too large file length
  path = GetPath(fbl::StringPrintf("%0*d", max_file_len + 1, 0xBEEF));
  ASSERT_EQ(open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644), -1);
  ASSERT_EQ(errno, ENAMETOOLONG);
}

// Hopefully not pushing against any 'max file length' boundaries, but large enough to fill a
// directory quickly.
constexpr int kLargePathLength = 128;

TEST_P(DirectoryTest, Large) {
  // Write a bunch of files to a directory.
  // FAT is very slow (see fxbug.dev/56389), so limit the number of files for that filesystem.
  const int num_files = fs().GetTraits().is_fat ? 100 : 1024;
  for (int i = 0; i < num_files; i++) {
    char path[kLargePathLength + 1];
    snprintf(path, sizeof(path), "%0*d", kLargePathLength - 2, i);
    fbl::unique_fd fd(open(GetPath(path).c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
    ASSERT_TRUE(fd) << path << ": " << strerror(errno);
  }

  // Unlink all those files
  for (int i = 0; i < num_files; i++) {
    char path[kLargePathLength + 1];
    snprintf(path, sizeof(path), "%0*d", kLargePathLength - 2, i);
    ASSERT_EQ(unlink(GetPath(path).c_str()), 0);
  }

  // TODO(smklein): Verify contents
}

void TestDirectoryCoalesceHelper(const std::string& base_path, const int* unlink_order) {
  const std::string files[] = {
      base_path + "/aaaaaaaa", base_path + "/bbbbbbbb", base_path + "/cccccccc",
      base_path + "/dddddddd", base_path + "/eeeeeeee",
  };
  int num_files = std::size(files);

  // Allocate a bunch of files in a directory
  ASSERT_EQ(mkdir(base_path.c_str(), 0755), 0);
  for (int i = 0; i < num_files; i++) {
    fbl::unique_fd fd(open(files[i].c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
    ASSERT_TRUE(fd);
  }

  // Unlink all those files in the order specified
  for (int i = 0; i < num_files; i++) {
    assert(0 <= unlink_order[i] && unlink_order[i] < num_files);
    ASSERT_EQ(unlink(files[unlink_order[i]].c_str()), 0);
  }

  ASSERT_EQ(rmdir(base_path.c_str()), 0);
}

TEST_P(DirectoryTest, DirectoryCoalesce) {
  // Test some cases of coalescing, assuming the directory was filled
  // according to allocation order. If it wasn't, this test should still pass,
  // but there is no mechanism to check the "location of a direntry in a
  // directory", so this is our best shot at "poking" the filesystem to try to
  // coalesce.

  // Case 1: Test merge-with-left
  const int merge_with_left[] = {0, 1, 2, 3, 4};
  ASSERT_NO_FATAL_FAILURE(TestDirectoryCoalesceHelper(GetPath("coalesce"), merge_with_left));

  // Case 2: Test merge-with-right
  const int merge_with_right[] = {4, 3, 2, 1, 0};
  ASSERT_NO_FATAL_FAILURE(TestDirectoryCoalesceHelper(GetPath("coalesce"), merge_with_right));

  // Case 3: Test merge-with-both
  const int merge_with_both[] = {1, 3, 2, 0, 4};
  ASSERT_NO_FATAL_FAILURE(TestDirectoryCoalesceHelper(GetPath("coalesce"), merge_with_both));
}

// This test prevents the regression of an fsck bug, which could also
// occur in a filesystem which does similar checks at runtime.
//
// This test ensures that if multiple large direntries are created
// and coalesced, the 'last remaining entry' still has a valid size,
// even though it may be quite large.
TEST_P(DirectoryTest, TestDirectoryCoalesceLargeRecord) {
  char buf[NAME_MAX + 1];
  ASSERT_EQ(mkdir(GetPath("coalesce_lr").c_str(), 0666), 0);
  fbl::unique_fd dirfd(open(GetPath("coalesce_lr").c_str(), O_RDONLY | O_DIRECTORY));
  ASSERT_GT(dirfd.get(), 0);

  const int kNumEntries = 20;

  // Make the entries
  for (int i = 0; i < kNumEntries; i++) {
    memset(buf, 'a' + i, 50);
    buf[50] = '\0';
    ASSERT_EQ(mkdirat(dirfd.get(), buf, 0666), 0);
  }

  // Unlink all the entries except the last one
  for (int i = 0; i < kNumEntries - 1; i++) {
    memset(buf, 'a' + i, 50);
    buf[50] = '\0';
    ASSERT_EQ(unlinkat(dirfd.get(), buf, AT_REMOVEDIR), 0);
  }

  // Check that the 'large remaining entry', which may
  // have a fairly large size, isn't marked as 'invalid' by
  // fsck.
  if (fs().GetTraits().can_unmount) {
    ASSERT_EQ(close(dirfd.release()), 0);
    EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
    dirfd.reset(open(GetPath("coalesce_lr").c_str(), O_RDONLY | O_DIRECTORY));
    ASSERT_GT(dirfd.get(), 0);
  }

  // Unlink the final entry
  memset(buf, 'a' + kNumEntries - 1, 50);
  buf[50] = '\0';
  ASSERT_EQ(unlinkat(dirfd.get(), buf, AT_REMOVEDIR), 0);

  ASSERT_EQ(close(dirfd.release()), 0);
  ASSERT_EQ(rmdir(GetPath("coalesce_lr").c_str()), 0);
}

TEST_P(DirectoryTest, TestDirectoryTrailingSlash) {
  // We should be able to refer to directories with any number of trailing
  // slashes, and still refer to the same entity.
  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("b/").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("c//").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("d///").c_str(), 0755), 0);

  ASSERT_EQ(rmdir(GetPath("a///").c_str()), 0);
  ASSERT_EQ(rmdir(GetPath("b//").c_str()), 0);
  ASSERT_EQ(rmdir(GetPath("c/").c_str()), 0);

  // Before we unlink 'd', try renaming it using some trailing '/' characters.
  ASSERT_EQ(rename(GetPath("d").c_str(), GetPath("e").c_str()), 0);
  ASSERT_EQ(rename(GetPath("e").c_str(), GetPath("d/").c_str()), 0);
  ASSERT_EQ(rename(GetPath("d/").c_str(), GetPath("e").c_str()), 0);
  ASSERT_EQ(rename(GetPath("e/").c_str(), GetPath("d/").c_str()), 0);
  ASSERT_EQ(rmdir(GetPath("d").c_str()), 0);

  // We can make / unlink a file...
  fbl::unique_fd fd(open(GetPath("a").c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(GetPath("a").c_str()), 0);

  // ... But we cannot refer to that file using a trailing '/'.
  fd.reset(open(GetPath("a").c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(open(GetPath("a/").c_str(), O_RDWR, 0644), -1);

  // We can rename the file...
  ASSERT_EQ(rename(GetPath("a").c_str(), GetPath("b").c_str()), 0);
  // ... But neither the source (nor the destination) can have trailing slashes.
  ASSERT_EQ(rename(GetPath("b").c_str(), GetPath("a/").c_str()), -1);
  ASSERT_EQ(rename(GetPath("b/").c_str(), GetPath("a").c_str()), -1);
  ASSERT_EQ(rename(GetPath("b/").c_str(), GetPath("a/").c_str()), -1);
  ASSERT_EQ(unlink(GetPath("b/").c_str()), -1);

  ASSERT_EQ(unlink(GetPath("b").c_str()), 0);
}

TEST_P(DirectoryTest, TestDirectoryReaddir) {
  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), -1);

  ExpectedDirectoryEntry empty_dir[] = {
      {".", DT_DIR},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("a").c_str(), empty_dir));

  ASSERT_EQ(mkdir(GetPath("a/dir1").c_str(), 0755), 0);
  fbl::unique_fd fd(open(GetPath("a/file1").c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);

  fd.reset(open(GetPath("a/file2").c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);

  ASSERT_EQ(mkdir(GetPath("a/dir2").c_str(), 0755), 0);
  ExpectedDirectoryEntry filled_dir[] = {
      {".", DT_DIR}, {"dir1", DT_DIR}, {"dir2", DT_DIR}, {"file1", DT_REG}, {"file2", DT_REG},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("a").c_str(), filled_dir));

  ASSERT_EQ(rmdir(GetPath("a/dir2").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("a/file2").c_str()), 0);
  ExpectedDirectoryEntry partial_dir[] = {
      {".", DT_DIR},
      {"dir1", DT_DIR},
      {"file1", DT_REG},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("a").c_str(), partial_dir));

  ASSERT_EQ(rmdir(GetPath("a/dir1").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("a/file1").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(GetPath("a").c_str(), empty_dir));
  ASSERT_EQ(unlink(GetPath("a").c_str()), 0);
}

TEST_P(DirectoryTest, TestDirectoryReaddirRmAll) {
  // FAT is very slow (see fxbug.dev/56389), so limit the number of directories for that filesystem.
  const size_t kNumEntries = fs().GetTraits().is_fat ? 100 : 1000;

  // Create a directory named GetPath("dir").c_str() with entries "00000", "00001" ... up to
  // kNumEntries.
  ASSERT_EQ(mkdir(GetPath("dir").c_str(), 0755), 0);

  // Create a large directory (ideally, large enough that our libc implementation can't cache the
  // entire contents of the directory with one 'getdirents' call).
  for (size_t i = 0; i < kNumEntries; i++) {
    char dirname[100];
    snprintf(dirname, 100, GetPath("dir/%05lu").c_str(), i);
    ASSERT_EQ(mkdir(dirname, 0755), 0);
  }

  DIR* dir = opendir(GetPath("dir").c_str());
  ASSERT_NE(dir, nullptr);

  // Unlink all the entries as we read them.
  struct dirent* de;
  size_t num_seen = 0;
  size_t i = 0;
  while ((de = readdir(dir)) != NULL) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      // Ignore these entries
      continue;
    }
    char dirname[100];
    snprintf(dirname, 100, "%05lu", i++);
    ASSERT_EQ(strcmp(de->d_name, dirname), 0) << "Unexpected dirent";
    ASSERT_EQ(unlinkat(dirfd(dir), dirname, AT_REMOVEDIR), 0);
    num_seen++;
  }

  ASSERT_EQ(num_seen, kNumEntries) << "Did not see all expected entries";
  ASSERT_EQ(closedir(dir), 0);
  ASSERT_EQ(rmdir(GetPath("dir").c_str()), 0);
}

TEST_P(DirectoryTest, TestDirectoryCreateAlternatingThenDeleteSucceeds) {
  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("b").c_str(), 0755), 0);

  constexpr ssize_t kNumEntries = 200;
  std::vector<fbl::unique_fd> fds;

  for (ssize_t i = 0; i < kNumEntries; i++) {
    // Create the files, leaving them open.
    fbl::unique_fd fd(open(GetPath(fbl::StringPrintf("a/%zd", i)).c_str(), O_CREAT | O_RDWR));
    ASSERT_TRUE(fd) << "a/" << i << ": " << strerror(errno);
    fds.push_back(std::move(fd));

    fd = fbl::unique_fd(open(GetPath(fbl::StringPrintf("b/%zd", i)).c_str(), O_CREAT | O_RDWR));
    ASSERT_TRUE(fd) << "b/" << i << ": " << strerror(errno);
    fds.push_back(std::move(fd));
  }

  for (ssize_t i = kNumEntries - 1; i >= 0; i--) {
    // Delete the files from one directory.
    ASSERT_EQ(unlink(GetPath(fbl::StringPrintf("a/%zd", i)).c_str()), 0)
        << "unlink a/" << i << ": " << strerror(errno);
  }
}

TEST_P(DirectoryTest, TestDirectoryRewind) {
  ASSERT_EQ(mkdir(GetPath("a").c_str(), 0755), 0);
  ExpectedDirectoryEntry empty_dir[] = {
      {".", DT_DIR},
  };

  DIR* dir = opendir(GetPath("a").c_str());
  ASSERT_NE(dir, nullptr);

  // We should be able to repeatedly access the directory without
  // re-opening it.
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, empty_dir));
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, empty_dir));

  ASSERT_EQ(mkdirat(dirfd(dir), "b", 0755), 0);
  ASSERT_EQ(mkdirat(dirfd(dir), "c", 0755), 0);

  // We should be able to modify the directory and re-process it without
  // re-opening it.
  ExpectedDirectoryEntry dir_contents[] = {
      {".", DT_DIR},
      {"b", DT_DIR},
      {"c", DT_DIR},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, dir_contents));
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, dir_contents));

  ASSERT_EQ(rmdir(GetPath("a/b").c_str()), 0);
  ASSERT_EQ(rmdir(GetPath("a/c").c_str()), 0);

  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, empty_dir));
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, empty_dir));

  ASSERT_EQ(closedir(dir), 0);
  ASSERT_EQ(rmdir(GetPath("a").c_str()), 0);
}

TEST_P(DirectoryTest, DirectoryAfterRmdir) {
  ExpectedDirectoryEntry empty_dir[] = {
      {".", DT_DIR},
  };

  // Make a directory...
  ASSERT_EQ(mkdir(GetPath("dir").c_str(), 0755), 0);
  DIR* dir = opendir(GetPath("dir").c_str());
  ASSERT_NE(dir, nullptr);
  // We can make and delete subdirectories, since GetPath("dir").c_str() exists...
  ASSERT_EQ(mkdir(GetPath("dir/subdir").c_str(), 0755), 0);
  ASSERT_EQ(rmdir(GetPath("dir/subdir").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, empty_dir));

  // Remove the directory. It's still open, so it should appear empty.
  ASSERT_EQ(rmdir(GetPath("dir").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, {}));

  // But we can't make new files / directories, by path...
  ASSERT_EQ(mkdir(GetPath("dir/subdir").c_str(), 0755), -1);
  // ... Or with the open fd.
  fbl::unique_fd fd(dirfd(dir));
  ASSERT_TRUE(fd);
  ASSERT_EQ(openat(fd.get(), "file", O_CREAT | O_RDWR), -1)
      << "Can't make new files in deleted dirs";
  ASSERT_EQ(mkdirat(fd.get(), "dir", 0755), -1) << "Can't make new files in deleted dirs";

  // In fact, the "dir" path should still be usable, even as a file!
  fbl::unique_fd fd2(open(GetPath("dir").c_str(), O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd2);
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, {}));
  ASSERT_EQ(close(fd2.release()), 0);
  ASSERT_EQ(unlink(GetPath("dir").c_str()), 0);

  // After all that, dir still looks like an empty directory...
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents(dir, {}));
  ASSERT_EQ(closedir(dir), 0);
}

TEST_P(DirectoryTest, RenameIntoUnlinkedDirectoryFails) {
  ASSERT_EQ(mkdir(GetPath("foo").c_str(), 0755), 0);
  fbl::unique_fd foo_fd(open(GetPath("foo").c_str(), O_RDONLY | O_DIRECTORY, 0644));
  ASSERT_TRUE(foo_fd);
  fbl::unique_fd baz_fd(open(GetPath("baz").c_str(), O_CREAT | O_RDWR));
  ASSERT_TRUE(baz_fd);
  fbl::unique_fd root_fd(open(GetPath("").c_str(), O_RDONLY | O_DIRECTORY, 0644));
  ASSERT_TRUE(root_fd);
  ASSERT_EQ(renameat(root_fd.get(), "baz", foo_fd.get(), "baz"), 0);
  ASSERT_EQ(renameat(foo_fd.get(), "baz", root_fd.get(), "baz"), 0);
  ASSERT_EQ(unlink(GetPath("foo").c_str()), 0);
  ASSERT_EQ(renameat(root_fd.get(), "baz", foo_fd.get(), "baz"), -1);
  ASSERT_EQ(errno, ENOENT);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, DirectoryTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
