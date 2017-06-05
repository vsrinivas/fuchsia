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
#include "misc.h"

bool check_link_count(const char* path, unsigned count) {
    struct stat s;
    ASSERT_EQ(stat(path, &s), 0, "");
    ASSERT_EQ(s.st_nlink, count, "");
    return true;
}

bool test_link_basic(void) {
    BEGIN_TEST;

    if (!test_info->supports_hardlinks) {
        return true;
    }

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
    ASSERT_TRUE(check_file_contents(fd, buf, sizeof(buf)), "");
    ASSERT_TRUE(check_link_count(oldpath, 1), "");

    ASSERT_EQ(link(oldpath, newpath), 0, "");
    ASSERT_TRUE(check_link_count(oldpath, 2), "");
    ASSERT_TRUE(check_link_count(newpath, 2), "");

    // Confirm that both the old link and the new links exist
    int fd2 = open(newpath, O_RDONLY, 0644);
    ASSERT_GT(fd2, 0, "");
    ASSERT_TRUE(check_file_contents(fd2, buf, sizeof(buf)), "");
    ASSERT_TRUE(check_file_contents(fd, buf, sizeof(buf)), "");

    // Remove the old link
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(close(fd2), 0, "");
    ASSERT_EQ(unlink(oldpath), 0, "");
    ASSERT_TRUE(check_link_count(newpath, 1), "");

    // Open the link by its new name, and verify that the contents have
    // not been altered by the removal of the old link.
    fd = open(newpath, O_RDONLY, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_TRUE(check_file_contents(fd, buf, sizeof(buf)), "");

    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink(newpath), 0, "");

    END_TEST;
}

bool test_link_count_dirs(void) {
    BEGIN_TEST;

    if (!test_info->supports_hardlinks) {
        return true;
    }

    ASSERT_EQ(mkdir("::dira", 0755), 0, "");
    // New directories should have two links:
    // Parent --> newdir
    // newdir ('.') --> newdir
    ASSERT_TRUE(check_link_count("::dira", 2), "");

    // Adding a file won't change the parent link count...
    int fd = open("::dira/file", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_TRUE(check_link_count("::dira", 2), "");
    ASSERT_TRUE(check_link_count("::dira/file", 1), "");

    // But adding a directory WILL change the parent link count.
    ASSERT_EQ(mkdir("::dira/dirb", 0755), 0, "");
    ASSERT_TRUE(check_link_count("::dira", 3), "");
    ASSERT_TRUE(check_link_count("::dira/dirb", 2), "");

    // Test that adding "depth" increases the dir count as we expect.
    ASSERT_EQ(mkdir("::dira/dirb/dirc", 0755), 0, "");
    ASSERT_TRUE(check_link_count("::dira", 3), "");
    ASSERT_TRUE(check_link_count("::dira/dirb", 3), "");
    ASSERT_TRUE(check_link_count("::dira/dirb/dirc", 2), "");

    // Demonstrate that unwinding also reduces the link count.
    ASSERT_EQ(unlink("::dira/dirb/dirc"), 0, "");
    ASSERT_TRUE(check_link_count("::dira", 3), "");
    ASSERT_TRUE(check_link_count("::dira/dirb", 2), "");

    ASSERT_EQ(unlink("::dira/dirb"), 0, "");
    ASSERT_TRUE(check_link_count("::dira", 2), "");

    // Test that adding "width" increases the dir count too.
    ASSERT_EQ(mkdir("::dira/dirb", 0755), 0, "");
    ASSERT_TRUE(check_link_count("::dira", 3), "");
    ASSERT_TRUE(check_link_count("::dira/dirb", 2), "");

    ASSERT_EQ(mkdir("::dira/dirc", 0755), 0, "");
    ASSERT_TRUE(check_link_count("::dira", 4), "");
    ASSERT_TRUE(check_link_count("::dira/dirb", 2), "");
    ASSERT_TRUE(check_link_count("::dira/dirc", 2), "");

    // Demonstrate that unwinding also reduces the link count.
    ASSERT_EQ(unlink("::dira/dirc"), 0, "");
    ASSERT_TRUE(check_link_count("::dira", 3), "");
    ASSERT_TRUE(check_link_count("::dira/dirb", 2), "");

    ASSERT_EQ(unlink("::dira/dirb"), 0, "");
    ASSERT_TRUE(check_link_count("::dira", 2), "");

    ASSERT_EQ(unlink("::dira/file"), 0, "");
    ASSERT_EQ(unlink("::dira"), 0, "");

    END_TEST;
}

bool test_link_count_rename(void) {
    BEGIN_TEST;

    if (!test_info->supports_hardlinks) {
        return true;
    }

    // Check that link count does not change with simple rename
    ASSERT_EQ(mkdir("::dir", 0755), 0, "");
    ASSERT_TRUE(check_link_count("::dir", 2), "");
    ASSERT_EQ(rename("::dir", "::dir_parent"), 0, "");
    ASSERT_TRUE(check_link_count("::dir_parent", 2), "");

    // Set up parent directory with child directories
    ASSERT_EQ(mkdir("::dir_parent/dir_child_a", 0755), 0, "");
    ASSERT_EQ(mkdir("::dir_parent/dir_child_b", 0755), 0, "");
    ASSERT_TRUE(check_link_count("::dir_parent", 4), "");
    ASSERT_TRUE(check_link_count("::dir_parent/dir_child_a", 2), "");
    ASSERT_TRUE(check_link_count("::dir_parent/dir_child_b", 2), "");

    // Rename a child directory out of its parent directory
    ASSERT_EQ(rename("::dir_parent/dir_child_b", "::dir_parent_alt"), 0, "");
    ASSERT_TRUE(check_link_count("::dir_parent", 3), "");
    ASSERT_TRUE(check_link_count("::dir_parent/dir_child_a", 2), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt", 2), "");

    // Rename a parent directory into another directory
    ASSERT_EQ(rename("::dir_parent", "::dir_parent_alt/dir_semi_parent"), 0, "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt", 3), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt/dir_semi_parent", 3), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt/dir_semi_parent/dir_child_a", 2), "");

    // Rename a directory on top of an empty directory
    ASSERT_EQ(mkdir("::dir_child", 0755), 0, "");
    ASSERT_EQ(rename("::dir_child", "::dir_parent_alt/dir_semi_parent/dir_child_a"), 0, "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt", 3), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt/dir_semi_parent", 3), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt/dir_semi_parent/dir_child_a", 2), "");

    // Rename a directory on top of an empty directory from a non-root directory
    ASSERT_EQ(mkdir("::dir", 0755), 0, "");
    ASSERT_EQ(mkdir("::dir/dir_child", 0755), 0, "");
    ASSERT_TRUE(check_link_count("::dir", 3), "");
    ASSERT_TRUE(check_link_count("::dir/dir_child", 2), "");
    ASSERT_EQ(rename("::dir/dir_child", "::dir_parent_alt/dir_semi_parent/dir_child_a"), 0, "");
    ASSERT_TRUE(check_link_count("::dir", 2), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt", 3), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt/dir_semi_parent", 3), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt/dir_semi_parent/dir_child_a", 2), "");

    // Rename a file on top of a file from a non-root directory
    ASSERT_EQ(unlink("::dir_parent_alt/dir_semi_parent/dir_child_a"), 0, "");
    int fd = open("::dir/dir_child", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_TRUE(check_link_count("::dir", 2), "");
    ASSERT_TRUE(check_link_count("::dir/dir_child", 1), "");
    int fd2 = open("::dir_parent_alt/dir_semi_parent/dir_child_a", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd2, 0, "");
    ASSERT_EQ(rename("::dir/dir_child", "::dir_parent_alt/dir_semi_parent/dir_child_a"), 0, "");
    ASSERT_TRUE(check_link_count("::dir", 2), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt", 3), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt/dir_semi_parent", 2), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt/dir_semi_parent/dir_child_a", 1), "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(close(fd2), 0, "");

    // Clean up
    ASSERT_EQ(unlink("::dir_parent_alt/dir_semi_parent/dir_child_a"), 0, "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt", 3), "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt/dir_semi_parent", 2), "");
    ASSERT_EQ(unlink("::dir_parent_alt/dir_semi_parent"), 0, "");
    ASSERT_TRUE(check_link_count("::dir_parent_alt", 2), "");
    ASSERT_EQ(unlink("::dir_parent_alt"), 0, "");
    ASSERT_EQ(unlink("::dir"), 0, "");

    END_TEST;
}

bool test_link_between_dirs(void) {
    BEGIN_TEST;

    if (!test_info->supports_hardlinks) {
        return true;
    }

    ASSERT_EQ(mkdir("::dira", 0755), 0, "");
    // New directories should have two links:
    // Parent --> newdir
    // newdir ('.') --> newdir
    ASSERT_TRUE(check_link_count("::dira", 2), "");

    ASSERT_EQ(mkdir("::dirb", 0755), 0, "");
    ASSERT_TRUE(check_link_count("::dirb", 2), "");

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
    ASSERT_TRUE(check_file_contents(fd, buf, sizeof(buf)), "");

    ASSERT_EQ(link(oldpath, newpath), 0, "");

    // Confirm that both the old link and the new links exist
    int fd2 = open(newpath, O_RDWR, 0644);
    ASSERT_GT(fd2, 0, "");
    ASSERT_TRUE(check_file_contents(fd2, buf, sizeof(buf)), "");
    ASSERT_TRUE(check_file_contents(fd, buf, sizeof(buf)), "");

    // Remove the old link
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(close(fd2), 0, "");
    ASSERT_EQ(unlink(oldpath), 0, "");

    // Open the link by its new name
    fd = open(newpath, O_RDWR, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_TRUE(check_file_contents(fd, buf, sizeof(buf)), "");

    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink(newpath), 0, "");
    ASSERT_EQ(unlink("::dira"), 0, "");
    ASSERT_EQ(unlink("::dirb"), 0, "");

    END_TEST;
}

bool test_link_errors(void) {
    BEGIN_TEST;

    if (!test_info->supports_hardlinks) {
        return true;
    }

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
    RUN_TEST_MEDIUM(test_link_count_dirs)
    RUN_TEST_MEDIUM(test_link_count_rename)
    RUN_TEST_MEDIUM(test_link_between_dirs)
    RUN_TEST_MEDIUM(test_link_errors)
)
