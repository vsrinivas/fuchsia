// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "filesystems.h"
#include "misc.h"

// Given a buffer of size PATH_MAX, make a 'len' byte long filename (not including null) consisting
// of the character 'c'.
static void make_name(char* buf, size_t len, char c) {
  memset(buf, ':', 2);
  buf += 2;
  memset(buf, c, len);
  buf[len] = '\0';
}

// Extends 'name' with a string 'len' bytes long, of the character 'c'.
// Assumes 'name' is large enough to hold 'len' additional bytes (and a new null character).
static void extend_name(char* name, size_t len, char c) {
  char buf[PATH_MAX];
  assert(len < PATH_MAX);
  memset(buf, c, len);
  buf[len] = '\0';
  strcat(name, "/");
  strcat(name, buf);
}

bool test_overflow_name(void) {
  BEGIN_TEST;

  char name_largest[PATH_MAX];
  char name_largest_alt[PATH_MAX];
  char name_too_large[PATH_MAX];
  make_name(name_largest, NAME_MAX, 'a');
  make_name(name_largest_alt, NAME_MAX, 'b');
  make_name(name_too_large, NAME_MAX + 1, 'a');

  // Try opening, closing, renaming, and unlinking the largest acceptable name
  int fd = open(name_largest, O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0, "");
  ASSERT_EQ(close(fd), 0, "");
  ASSERT_EQ(rename(name_largest, name_largest_alt), 0, "");
  ASSERT_EQ(rename(name_largest_alt, name_largest), 0, "");

  ASSERT_EQ(rename(name_largest, name_too_large), -1, "");
  ASSERT_EQ(rename(name_too_large, name_largest), -1, "");
  ASSERT_EQ(unlink(name_largest), 0, "");

  // Try it with a directory too
  ASSERT_EQ(mkdir(name_largest, 0755), 0, "");
  ASSERT_EQ(rename(name_largest, name_largest_alt), 0, "");
  ASSERT_EQ(rename(name_largest_alt, name_largest), 0, "");

  ASSERT_EQ(rename(name_largest, name_too_large), -1, "");
  ASSERT_EQ(rename(name_too_large, name_largest), -1, "");
  ASSERT_EQ(unlink(name_largest), 0, "");

  // Try opening an unacceptably large name
  ASSERT_EQ(open(name_too_large, O_RDWR | O_CREAT | O_EXCL, 0644), -1, "");
  // Try it with a directory too
  ASSERT_EQ(mkdir(name_too_large, 0755), -1, "");

  END_TEST;
}

bool test_overflow_path(void) {
  BEGIN_TEST;

  // Make the name buffer larger than PATH_MAX so we don't overflow
  char name[2 * PATH_MAX];

  int depth = 0;

  // Create an initial directory
  make_name(name, NAME_MAX, 'a');
  ASSERT_EQ(mkdir(name, 0755), 0, "");
  depth++;
  // Create child directories until we hit PATH_MAX
  while (true) {
    extend_name(name, NAME_MAX, 'a');
    int r = mkdir(name, 0755);
    if (r < 0) {
      assert(errno == ENAMETOOLONG);
      break;
    }
    depth++;
  }

  // Remove all child directories
  while (depth != 0) {
    char* last_slash = strrchr(name, '/');
    assert(last_slash != NULL);
    assert(*last_slash == '/');
    *last_slash = '\0';
    ASSERT_EQ(unlink(name), 0, "");
    depth--;
  }

  END_TEST;
}

bool test_overflow_integer(void) {
  BEGIN_TEST;

  int fd = open("::file", O_CREAT | O_RDWR | O_EXCL, 0644);
  ASSERT_GT(fd, 0, "");

  // TODO(smklein): Test extremely large reads/writes when remoteio can handle them without
  // crashing
  /*
  char buf[4096];
  ASSERT_EQ(write(fd, buf, SIZE_MAX - 1), -1, "");
  ASSERT_EQ(write(fd, buf, SIZE_MAX), -1, "");

  ASSERT_EQ(read(fd, buf, SIZE_MAX - 1), -1, "");
  ASSERT_EQ(read(fd, buf, SIZE_MAX), -1, "");
  */

  ASSERT_EQ(ftruncate(fd, INT_MIN), -1, "");
  ASSERT_EQ(ftruncate(fd, -1), -1, "");
  ASSERT_EQ(ftruncate(fd, SIZE_MAX - 1), -1, "");
  ASSERT_EQ(ftruncate(fd, SIZE_MAX), -1, "");

  ASSERT_EQ(lseek(fd, INT_MIN, SEEK_SET), -1, "");
  ASSERT_EQ(lseek(fd, -1, SEEK_SET), -1, "");
  ASSERT_EQ(lseek(fd, SIZE_MAX - 1, SEEK_SET), -1, "");
  ASSERT_EQ(lseek(fd, SIZE_MAX, SEEK_SET), -1, "");
  ASSERT_EQ(close(fd), 0, "");
  ASSERT_EQ(unlink("::file"), 0, "");

  END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(overflow_tests,
                        RUN_TEST_MEDIUM(test_overflow_name) RUN_TEST_MEDIUM(test_overflow_path)
                            RUN_TEST_MEDIUM(test_overflow_integer))
