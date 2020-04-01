// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zircon/compiler.h>
#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>

#include "filesystems.h"
#include "misc.h"

// Make some files, then unlink them.
bool TestUnlinkSimple() {
  BEGIN_TEST;
  const char* const paths[] = {"::abc", "::def", "::ghi", "::jkl", "::mnopqrstuvxyz"};
  for (size_t i = 0; i < fbl::count_of(paths); i++) {
    fbl::unique_fd fd(open(paths[i], O_RDWR | O_CREAT | O_EXCL, 0644));
    ASSERT_TRUE(fd);
  }
  for (size_t i = 0; i < fbl::count_of(paths); i++) {
    ASSERT_EQ(unlink(paths[i]), 0);
  }
  END_TEST;
}

const char* const STRING_DATA[] = {
    "Hello, world",
    "Foo bar baz blat",
    "This is yet another sample string",
};

static bool simple_read_test(int fd, size_t data_index) {
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  ssize_t len = strlen(STRING_DATA[data_index]);
  ASSERT_EQ(read(fd, buf, len), len);
  ASSERT_EQ(memcmp(STRING_DATA[data_index], buf, strlen(STRING_DATA[data_index])), 0);
  return true;
}

static bool simple_write_test(int fd, size_t data_index) {
  ASSERT_EQ(ftruncate(fd, 0), 0);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  ssize_t len = strlen(STRING_DATA[data_index]);
  ASSERT_EQ(write(fd, STRING_DATA[data_index], len), len);
  return simple_read_test(fd, data_index);
}

bool TestUnlinkUseAfterwards() {
  BEGIN_TEST;

  const char* path = "::foobar";
  fbl::unique_fd fd(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);

  ASSERT_TRUE(simple_write_test(fd.get(), 1));

  // When we unlink path, fd is still open.
  ASSERT_EQ(unlink(path), 0);
  ASSERT_TRUE(simple_read_test(fd.get(), 1));   // It should contain the same data as before
  ASSERT_TRUE(simple_write_test(fd.get(), 2));  // It should still be writable
  ASSERT_EQ(close(fd.release()), 0);            // This actually releases the file

  // Now, opening the file should fail without O_CREAT
  ASSERT_EQ(open(path, O_RDWR, 0644), -1);

  END_TEST;
}

bool TestUnlinkOpenElsewhere() {
  BEGIN_TEST;

  const char* path = "::foobar";
  fbl::unique_fd fd1(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd1);
  fbl::unique_fd fd2(open(path, O_RDWR, 0644));
  ASSERT_TRUE(fd2);

  ASSERT_TRUE(simple_write_test(fd1.get(), 0));
  ASSERT_EQ(close(fd1.release()), 0);

  // When we unlink path, fd2 is still open.
  ASSERT_EQ(unlink(path), 0);
  ASSERT_TRUE(simple_read_test(fd2.get(), 0));   // It should contain the same data as before
  ASSERT_TRUE(simple_write_test(fd2.get(), 1));  // It should still be writable
  ASSERT_EQ(close(fd2.release()), 0);            // This actually releases the file

  // Now, opening the file should fail without O_CREAT
  ASSERT_EQ(open(path, O_RDWR, 0644), -1);

  END_TEST;
}

bool TestRemove() {
  BEGIN_TEST;

  // Test the trivial cases of removing files and directories
  const char* filename = "::file";
  fbl::unique_fd fd(open(filename, O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(remove(filename), 0);
  ASSERT_EQ(remove(filename), -1);
  ASSERT_EQ(errno, ENOENT);
  ASSERT_EQ(close(fd.release()), 0);

  const char* dirname = "::dir";
  ASSERT_EQ(mkdir(dirname, 0666), 0);
  ASSERT_EQ(remove(dirname), 0);
  ASSERT_EQ(remove(dirname), -1);
  ASSERT_EQ(errno, ENOENT);

  // Test that we cannot remove non-empty directories, and that
  // we see the expected error code too.
  ASSERT_EQ(mkdir("::dir", 0666), 0);
  ASSERT_EQ(mkdir("::dir/subdir", 0666), 0);
  ASSERT_EQ(remove("::dir"), -1);
  ASSERT_EQ(errno, ENOTEMPTY);
  ASSERT_EQ(remove("::dir/subdir"), 0);
  ASSERT_EQ(remove("::dir"), 0);
  ASSERT_EQ(remove("::dir"), -1);
  ASSERT_EQ(errno, ENOENT);

  END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(unlink_tests,
                        RUN_TEST_MEDIUM(TestUnlinkSimple)
                        RUN_TEST_MEDIUM(TestUnlinkUseAfterwards)
                        RUN_TEST_MEDIUM(TestUnlinkOpenElsewhere)
                        RUN_TEST_MEDIUM(TestRemove))
