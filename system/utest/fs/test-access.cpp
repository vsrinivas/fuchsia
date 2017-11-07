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

    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);

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

bool test_access_opath(void) {
    BEGIN_TEST;

    const char* dirname = "::foo";
    const char* filename = "::foo/bar";

    ASSERT_EQ(mkdir(dirname, 0666), 0);
    int fd;

    // Cannot create a file as O_PATH
    fd = open(filename, O_CREAT | O_RDWR | O_PATH);
    ASSERT_LT(fd, 0);

    const char* data = "hello";
    const size_t datalen = strlen(data);

    fd = open(filename, O_CREAT | O_RDWR);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(write(fd, data, datalen), static_cast<ssize_t>(datalen));
    ASSERT_EQ(close(fd), 0);

    // Cannot read to / write from O_PATH fd
    fd = open(filename, O_RDWR | O_PATH);
    ASSERT_GE(fd, 0);

    char buf[128];
    ASSERT_LT(read(fd, buf, sizeof(buf)), 0);
    ASSERT_EQ(errno, EBADF);
    ASSERT_LT(write(fd, data, datalen), 0);
    ASSERT_EQ(errno, EBADF);
    ASSERT_LT(lseek(fd, 1, SEEK_SET), 0);
    ASSERT_EQ(errno, EBADF);

    // We can fstat the file, however
    struct stat st;
    ASSERT_EQ(fstat(fd, &st), 0);
    ASSERT_EQ(st.st_size, static_cast<ssize_t>(datalen));
    ASSERT_EQ(close(fd), 0);

    // We can pass in a variety of flags to open with O_PATH, and
    // they'll be ignored.
    fd = open(filename, O_RDWR | O_CREAT | O_EXCL | O_TRUNC | O_PATH);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(fstat(fd, &st), 0);
    ASSERT_EQ(st.st_size, static_cast<ssize_t>(datalen));

    // We can use fcntl on the fd
    int flags = fcntl(fd, F_GETFL);
    ASSERT_GT(flags, -1);
    ASSERT_EQ(flags & O_ACCMODE, O_PATH);
    ASSERT_EQ(flags & ~O_ACCMODE, 0);

    // We can toggle some flags, even if they don't make much sense
    ASSERT_EQ(fcntl(fd, F_SETFL, flags | O_APPEND), 0);
    flags = fcntl(fd, F_GETFL);
    ASSERT_EQ(flags & O_ACCMODE, O_PATH);
    ASSERT_EQ(flags & ~O_ACCMODE, O_APPEND);
    // We still can't write though
    ASSERT_LT(write(fd, data, datalen), 0);
    ASSERT_EQ(errno, EBADF);

    // We cannot update attributes of the file
    struct timespec ts[2];
    ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = 0;
    ts[1].tv_nsec = 0;
    ASSERT_LT(futimens(fd, ts), 0);
    ASSERT_EQ(errno, EBADF);
    ASSERT_EQ(close(fd), 0);

    // O_PATH doesn't ignore O_DIRECTORY
    ASSERT_LT(open(filename, O_PATH | O_DIRECTORY), 0);

    // We can use O_PATH when opening directories too
    fd = open(dirname, O_PATH | O_DIRECTORY);
    ASSERT_GE(fd, 0);

    // The *at functions are allowed
    ASSERT_EQ(renameat(fd, "bar", fd, "baz"), 0);
    if (test_info->supports_hardlinks) {
        // TODO(smklein): Implement linkat, use it here
        // ASSERT_EQ(linkat(fd, "baz", fd, "bar", 0), 0);
        ASSERT_EQ(link("::foo/baz", filename), 0);
        ASSERT_EQ(unlinkat(fd, "baz", 0), 0);
    } else {
        ASSERT_EQ(renameat(fd, "baz", fd, "bar"), 0);
    }

    // Readdir is not allowed
    DIR* dir = fdopendir(fd);
    ASSERT_NONNULL(dir);
    struct dirent* de = readdir(dir);
    ASSERT_NULL(de);
    ASSERT_EQ(errno, EBADF);
    ASSERT_EQ(closedir(dir), 0);

    ASSERT_EQ(unlink(filename), 0);
    ASSERT_EQ(rmdir(dirname), 0);

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(access_tests,
    RUN_TEST_MEDIUM(test_access_readable)
    RUN_TEST_MEDIUM(test_access_writable)
    RUN_TEST_MEDIUM(test_access_badflags)
    RUN_TEST_MEDIUM(test_access_directory)
    RUN_TEST_MEDIUM(test_access_opath)
)
