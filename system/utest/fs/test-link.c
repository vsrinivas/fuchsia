// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/compiler.h>

#include "filesystems.h"

// Check the contents of a file are what we expect
bool confirm_contents(int fd, uint8_t* buf, size_t length) {
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    uint8_t* out = malloc(length);
    assert(out != NULL);
    ASSERT_STREAM_ALL(read, fd, out, length);
    ASSERT_EQ(memcmp(buf, out, length), 0, "");
    free(out);
    return true;
}

bool test_link_basic(void) {
    if (!test_info->supports_hardlinks) {
        return true;
    }
    BEGIN_TEST;

    const char* oldpath = "::a";
    const char* newpath = "::b";

    // Make a file, fill it with content
    int fd = open(oldpath, O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    uint8_t buf[100];
    for (size_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (uint8_t) rand();
    }
    ASSERT_STREAM_ALL(write, fd, buf, sizeof(buf));
    ASSERT_TRUE(confirm_contents(fd, buf, sizeof(buf)), "");

    ASSERT_EQ(link(oldpath, newpath), 0, "");

    // Confirm that both the old link and the new links exist
    int fd2 = open(newpath, O_RDONLY, 0644);
    ASSERT_GT(fd2, 0, "");
    ASSERT_TRUE(confirm_contents(fd2, buf, sizeof(buf)), "");
    ASSERT_TRUE(confirm_contents(fd, buf, sizeof(buf)), "");

    // Remove the old link
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(close(fd2), 0, "");
    ASSERT_EQ(unlink(oldpath), 0, "");

    // Open the link by its new name, and verify that the contents have
    // not been altered by the removal of the old link.
    fd = open(newpath, O_RDONLY, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_TRUE(confirm_contents(fd, buf, sizeof(buf)), "");

    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink(newpath), 0, "");

    END_TEST;
}

bool test_link_between_dirs(void) {
    if (!test_info->supports_hardlinks) {
        return true;
    }
    BEGIN_TEST;

    ASSERT_EQ(mkdir("::dira", 0755), 0, "");
    ASSERT_EQ(mkdir("::dirb", 0755), 0, "");
    const char* oldpath = "::dira/a";
    const char* newpath = "::dirb/b";

    // Make a file, fill it with content
    int fd = open(oldpath, O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    uint8_t buf[100];
    for (size_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (uint8_t) rand();
    }
    ASSERT_STREAM_ALL(write, fd, buf, sizeof(buf));
    ASSERT_TRUE(confirm_contents(fd, buf, sizeof(buf)), "");

    ASSERT_EQ(link(oldpath, newpath), 0, "");

    // Confirm that both the old link and the new links exist
    int fd2 = open(newpath, O_RDWR, 0644);
    ASSERT_GT(fd2, 0, "");
    ASSERT_TRUE(confirm_contents(fd2, buf, sizeof(buf)), "");
    ASSERT_TRUE(confirm_contents(fd, buf, sizeof(buf)), "");

    // Remove the old link
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(close(fd2), 0, "");
    ASSERT_EQ(unlink(oldpath), 0, "");

    // Open the link by its new name
    fd = open(newpath, O_RDWR, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_TRUE(confirm_contents(fd, buf, sizeof(buf)), "");

    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink(newpath), 0, "");
    ASSERT_EQ(unlink("::dira"), 0, "");
    ASSERT_EQ(unlink("::dirb"), 0, "");

    END_TEST;
}

bool test_link_errors(void) {
    if (!test_info->supports_hardlinks) {
        return true;
    }
    BEGIN_TEST;

    const char* dirpath = "::dir";
    const char* oldpath = "::a";
    const char* newpath = "::b";
    const char* newpathdir = "::b/";

    // We should not be able to create hard links to directories
    ASSERT_EQ(mkdir(dirpath, 0755), 0, "");
    ASSERT_EQ(link(dirpath, newpath), -1, "");
    ASSERT_EQ(unlink(dirpath), 0, "");

    // We should not be able to create hard links to non-existent files
    ASSERT_EQ(link(oldpath, newpath), -1, "");
    ASSERT_EQ(errno, ENOENT, "");

    int fd = open(oldpath, O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");

    // We should not be able to link to or from . or ..
    ASSERT_EQ(link(oldpath, "::."), -1, "");
    ASSERT_EQ(link(oldpath, "::.."), -1, "");
    ASSERT_EQ(link("::.", newpath), -1, "");
    ASSERT_EQ(link("::..", newpath), -1, "");

    // We should not be able to link a file to itself
    ASSERT_EQ(link(oldpath, oldpath), -1, "");
    ASSERT_EQ(errno, EEXIST, "");

    // We should not be able to link a file to a path that implies it must be a directory
    ASSERT_EQ(link(oldpath, newpathdir), -1, "");

    // After linking, we shouldn't be able to link again
    ASSERT_EQ(link(oldpath, newpath), 0, "");
    ASSERT_EQ(link(oldpath, newpath), -1, "");
    ASSERT_EQ(errno, EEXIST, "");
    // In either order
    ASSERT_EQ(link(newpath, oldpath), -1, "");
    ASSERT_EQ(errno, EEXIST, "");

    ASSERT_EQ(unlink(newpath), 0, "");
    ASSERT_EQ(unlink(oldpath), 0, "");

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(hard_link_tests,
    RUN_TEST_MEDIUM(test_link_basic)
    RUN_TEST_MEDIUM(test_link_between_dirs)
    RUN_TEST_MEDIUM(test_link_errors)
)
