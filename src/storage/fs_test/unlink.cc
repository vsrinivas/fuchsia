// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <string_view>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using UnlinkTest = FilesystemTest;

// Make some files, then unlink them.
TEST_P(UnlinkTest, Simple) {
  const std::string paths[] = {GetPath("abc"), GetPath("def"), GetPath("ghi"), GetPath("jkl"),
                               GetPath("mnopqrstuvxyz")};
  for (size_t i = 0; i < std::size(paths); i++) {
    fbl::unique_fd fd(open(paths[i].c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
    ASSERT_TRUE(fd);
  }
  for (size_t i = 0; i < std::size(paths); i++) {
    ASSERT_EQ(unlink(paths[i].c_str()), 0);
  }
}

constexpr std::string_view kStringData[] = {
    "Hello, world",
    "Foo bar baz blat",
    "This is yet another sample string",
};

void SimpleReadTest(int fd, size_t data_index) {
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  ssize_t len = kStringData[data_index].size();
  ASSERT_EQ(read(fd, buf, len), static_cast<ssize_t>(len));
  ASSERT_EQ(memcmp(kStringData[data_index].data(), buf, len), 0);
}

void SimpleWriteTest(int fd, size_t data_index) {
  ASSERT_EQ(ftruncate(fd, 0), 0);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  ssize_t len = kStringData[data_index].size();
  ASSERT_EQ(write(fd, kStringData[data_index].data(), len), static_cast<ssize_t>(len));
  SimpleReadTest(fd, data_index);
}

TEST_P(UnlinkTest, UseAfterwards) {
  const std::string path = GetPath("foobar");
  fbl::unique_fd fd(open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);

  ASSERT_NO_FATAL_FAILURE(SimpleWriteTest(fd.get(), 1));

  // When we unlink path, fd is still open.
  ASSERT_EQ(unlink(path.c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(
      SimpleReadTest(fd.get(), 1));  // It should contain the same data as before
  ASSERT_NO_FATAL_FAILURE(SimpleWriteTest(fd.get(), 2));  // It should still be writable
  ASSERT_EQ(close(fd.release()), 0);                      // This actually releases the file

  // Now, opening the file should fail without O_CREAT
  ASSERT_EQ(open(path.c_str(), O_RDWR, 0644), -1);
}

TEST_P(UnlinkTest, UseAfterRenameOver) {
  const std::string path = GetPath("foobar");
  fbl::unique_fd fd(open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);

  ASSERT_NO_FATAL_FAILURE(SimpleWriteTest(fd.get(), 1));

  // When we rename over path, fd is still open.
  const std::string barfoo = GetPath("barfoo");
  fbl::unique_fd fd2(open(barfoo.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_EQ(rename(barfoo.c_str(), path.c_str()), 0);

  ASSERT_NO_FATAL_FAILURE(
      SimpleReadTest(fd.get(), 1));  // It should contain the same data as before
  ASSERT_NO_FATAL_FAILURE(SimpleWriteTest(fd.get(), 2));  // It should still be writable
}

TEST_P(UnlinkTest, OpenElsewhere) {
  const std::string path = GetPath("foobar");
  fbl::unique_fd fd1(open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd1);
  fbl::unique_fd fd2(open(path.c_str(), O_RDWR, 0644));
  ASSERT_TRUE(fd2);

  ASSERT_NO_FATAL_FAILURE(SimpleWriteTest(fd1.get(), 0));
  ASSERT_EQ(close(fd1.release()), 0);

  // When we unlink path, fd2 is still open.
  ASSERT_EQ(unlink(path.c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(
      SimpleReadTest(fd2.get(), 0));  // It should contain the same data as before
  ASSERT_NO_FATAL_FAILURE(SimpleWriteTest(fd2.get(), 1));  // It should still be writable
  ASSERT_EQ(close(fd2.release()), 0);                      // This actually releases the file

  // Now, opening the file should fail without O_CREAT
  ASSERT_EQ(open(path.c_str(), O_RDWR, 0644), -1);
}

TEST_P(UnlinkTest, OpenElsewhereLongName) {
  // Test a filename that's not 8.3
  const std::string path = GetPath("really_really_long_file_name");
  fbl::unique_fd fd1(open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd1);
  fbl::unique_fd fd2(open(path.c_str(), O_RDWR, 0644));
  ASSERT_TRUE(fd2);

  ASSERT_NO_FATAL_FAILURE(SimpleWriteTest(fd1.get(), 0));
  ASSERT_EQ(close(fd1.release()), 0);

  // When we unlink path, fd2 is still open.
  ASSERT_EQ(unlink(path.c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(
      SimpleReadTest(fd2.get(), 0));  // It should contain the same data as before
  ASSERT_NO_FATAL_FAILURE(SimpleWriteTest(fd2.get(), 1));  // It should still be writable
  ASSERT_EQ(close(fd2.release()), 0);                      // This actually releases the file

  // Now, opening the file should fail without O_CREAT
  ASSERT_EQ(open(path.c_str(), O_RDWR, 0644), -1);
}

TEST_P(UnlinkTest, Remove) {
  // Test the trivial cases of removing files and directories
  const std::string filename = GetPath("file");
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(remove(filename.c_str()), 0);
  ASSERT_EQ(remove(filename.c_str()), -1);
  ASSERT_EQ(errno, ENOENT);
  ASSERT_EQ(close(fd.release()), 0);

  const std::string dirname = GetPath("dir");
  ASSERT_EQ(mkdir(dirname.c_str(), 0666), 0);
  ASSERT_EQ(remove(dirname.c_str()), 0);
  ASSERT_EQ(remove(dirname.c_str()), -1);
  ASSERT_EQ(errno, ENOENT);

  // Test that we cannot remove non-empty directories, and that
  // we see the expected error code too.
  ASSERT_EQ(mkdir(dirname.c_str(), 0666), 0);
  ASSERT_EQ(mkdir((dirname + "/subdir").c_str(), 0666), 0);
  ASSERT_EQ(remove(dirname.c_str()), -1);
  ASSERT_EQ(errno, ENOTEMPTY);
  ASSERT_EQ(remove((dirname + "/subdir").c_str()), 0);
  ASSERT_EQ(remove(dirname.c_str()), 0);
  ASSERT_EQ(remove(dirname.c_str()), -1);
  ASSERT_EQ(errno, ENOENT);
}

using UnlinkSparseTest = FilesystemTest;

TEST_P(UnlinkSparseTest, UnlinkLargeSparseFileAfterClose) {
  const std::string foo = GetPath("foo");
  fbl::unique_fd fd(open(foo.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  // The offset here is deliberately chosen so that it would involve Minfs's double indirect blocks,
  // but also fits within memfs.
  ASSERT_EQ(pwrite(fd.get(), "hello", 5, 0x20000000 - 5), 5);
  ASSERT_EQ(fsync(fd.get()), 0);  // Deliberate sync so that close is likely to unload the vnode.
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(foo.c_str()), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, UnlinkTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

// These tests will only work on a file system that supports sparse files.
INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, UnlinkSparseTest,
    testing::ValuesIn(MapAndFilterAllTestFilesystems(
        [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
          if (options.filesystem->GetTraits().supports_sparse_files) {
            return options;
          } else {
            return std::nullopt;
          }
        })),
    testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
