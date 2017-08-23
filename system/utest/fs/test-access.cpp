// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "filesystems.h"
#include "misc.h"

bool test_access_readable(void) {
    BEGIN_TEST;

    const char* filename = "::alpha";

    // Try writing a string to a file
    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);
    const char buf[] = "Hello, World!\n";
    ASSERT_EQ(write(fd, buf, sizeof(buf)), sizeof(buf));
    ASSERT_EQ(close(fd), 0);

    // Re-open as readonly
    fd = open(filename, O_RDONLY, 0644);

    // Reading is allowed
    char tmp[sizeof(buf)];
    ASSERT_EQ(read(fd, tmp, sizeof(tmp)), sizeof(tmp));
    ASSERT_EQ(memcmp(buf, tmp, sizeof(tmp)), 0);

    // Writing is disallowed
    ASSERT_EQ(write(fd, buf, sizeof(buf)), -1);
    ASSERT_EQ(errno, EBADF);
    errno = 0;

    // Truncating is disallowed
    ASSERT_EQ(ftruncate(fd, 0), -1);
    ASSERT_EQ(errno, EBADF);
    errno = 0;

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink(filename), 0);

    END_TEST;
}

bool test_access_writable(void) {
    BEGIN_TEST;

    const char* filename = "::alpha";

    // Try writing a string to a file
    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);
    const char buf[] = "Hello, World!\n";
    ASSERT_EQ(write(fd, buf, sizeof(buf)), sizeof(buf));
    ASSERT_EQ(close(fd), 0);

    // Re-open as writable
    fd = open(filename, O_WRONLY, 0644);

    // Reading is disallowed
    char tmp[sizeof(buf)];
    ASSERT_EQ(read(fd, tmp, sizeof(tmp)), -1);
    ASSERT_EQ(errno, EBADF);
    errno = 0;

    // Writing is allowed
    ASSERT_EQ(write(fd, buf, sizeof(buf)), sizeof(buf));

    // Truncating is allowed
    ASSERT_EQ(ftruncate(fd, 0), 0);

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink(filename), 0);

    END_TEST;
}

bool test_access_badflags(void) {
    BEGIN_TEST;

    const char* filename = "::foobar";

    // No creation with "RDWR + WRONLY"
    ASSERT_LT(open(filename, O_RDWR | O_WRONLY | O_CREAT, 0644), 0);

    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);

    // No re-opening with "RDWR + WRONLY"
    ASSERT_LT(open(filename, O_RDWR | O_WRONLY, 0644), 0);

    // No read-only truncation
    ASSERT_LT(open(filename, O_RDONLY | O_TRUNC | O_CREAT, 0644), 0);

    ASSERT_EQ(unlink(filename), 0);

    END_TEST;
}

bool test_access_directory(void) {
    BEGIN_TEST;

    const char* filename = "::foobar";

    ASSERT_EQ(mkdir(filename, 0666), 0);

    // Try opening as writable
    int fd = open(filename, O_RDWR, 0644);
    ASSERT_LT(fd, 0);
    ASSERT_EQ(errno, EISDIR);
    fd = open(filename, O_WRONLY, 0644);
    ASSERT_LT(fd, 0);
    ASSERT_EQ(errno, EISDIR);

    // Directories should only be openable with O_RDONLY
    fd = open(filename, O_RDONLY, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(rmdir(filename), 0);

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(access_tests,
    RUN_TEST_MEDIUM(test_access_readable)
    RUN_TEST_MEDIUM(test_access_writable)
    RUN_TEST_MEDIUM(test_access_badflags)
    RUN_TEST_MEDIUM(test_access_directory)
)
