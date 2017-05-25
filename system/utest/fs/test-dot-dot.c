// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <magenta/compiler.h>

#include "filesystems.h"
#include "misc.h"

// Test cases of '..' where the path can be canonicalized on the client.
bool test_dot_dot_client(void) {
    BEGIN_TEST;
    ASSERT_EQ(mkdir("::foo", 0755), 0, "");
    ASSERT_EQ(mkdir("::foo/bit", 0755), 0, "");
    ASSERT_EQ(mkdir("::foo/bar", 0755), 0, "");
    ASSERT_EQ(mkdir("::foo/bar/baz", 0755), 0, "");

    expected_dirent_t foo_dir[] = {
        {false, ".", DT_DIR},
        {false, "..", DT_DIR},
        {false, "bar", DT_DIR},
        {false, "bit", DT_DIR},
    };

    expected_dirent_t bar_dir[] = {
        {false, ".", DT_DIR},
        {false, "..", DT_DIR},
        {false, "baz", DT_DIR},
    };

    // Test cases of client-side dot-dot when moving between directories.
    DIR* dir = opendir("::foo/bar/..");
    ASSERT_NONNULL(dir, "");
    ASSERT_TRUE(fcheck_dir_contents(dir, foo_dir, countof(foo_dir)), "");
    ASSERT_EQ(closedir(dir), 0, "");

    dir = opendir("::foo/bar/../bit/..//././//");
    ASSERT_NONNULL(dir, "");
    ASSERT_TRUE(fcheck_dir_contents(dir, foo_dir, countof(foo_dir)), "");
    ASSERT_EQ(closedir(dir), 0, "");

    dir = opendir("::foo/bar/baz/../../../foo/bar/baz/..");
    ASSERT_NONNULL(dir, "");
    ASSERT_TRUE(fcheck_dir_contents(dir, bar_dir, countof(bar_dir)), "");
    ASSERT_EQ(closedir(dir), 0, "");

    // Clean up
    ASSERT_EQ(unlink("::foo/bar/baz"), 0, "");
    ASSERT_EQ(unlink("::foo/bar"), 0, "");
    ASSERT_EQ(unlink("::foo/bit"), 0, "");
    ASSERT_EQ(unlink("::foo"), 0, "");
    END_TEST;
}

// Test cases of '..' where the path cannot be canonicalized on the client.
bool test_dot_dot_server(void) {
    BEGIN_TEST;
    ASSERT_EQ(mkdir("::foo", 0755), 0, "");
    ASSERT_EQ(mkdir("::foo/bit", 0755), 0, "");
    ASSERT_EQ(mkdir("::foo/bar", 0755), 0, "");
    ASSERT_EQ(mkdir("::foo/bar/baz", 0755), 0, "");

    expected_dirent_t foo_dir[] = {
        {false, ".", DT_DIR},
        {false, "..", DT_DIR},
        {false, "bar", DT_DIR},
        {false, "bit", DT_DIR},
    };

    expected_dirent_t bar_dir[] = {
        {false, ".", DT_DIR},
        {false, "..", DT_DIR},
        {false, "baz", DT_DIR},
    };

    int foo_fd = open("::foo", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(foo_fd, 0, "");

    // ".." from foo --> "foo"
    int fd = openat(foo_fd, "..", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0, "");
    DIR* dir = fdopendir(fd); // Consumes 'fd'
    ASSERT_NONNULL(dir, "");
    ASSERT_TRUE(fcheck_dir_contents(dir, foo_dir, countof(foo_dir)), "");
    ASSERT_EQ(closedir(dir), 0, "");

    // "bar/.." from foo --> "foo"
    fd = openat(foo_fd, "bar/..", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0, "");
    dir = fdopendir(fd); // Consumes 'fd'
    ASSERT_NONNULL(dir, "");
    ASSERT_TRUE(fcheck_dir_contents(dir, foo_dir, countof(foo_dir)), "");
    ASSERT_EQ(closedir(dir), 0, "");

    // "bar/../.." from foo --> "foo"
    fd = openat(foo_fd, "bar/../..", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0, "");
    dir = fdopendir(fd); // Consumes 'fd'
    ASSERT_NONNULL(dir, "");
    ASSERT_TRUE(fcheck_dir_contents(dir, foo_dir, countof(foo_dir)), "");
    ASSERT_EQ(closedir(dir), 0, "");

    // "../../../../../bar" --> "bar"
    fd = openat(foo_fd, "../../../../../bar", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0, "");
    dir = fdopendir(fd); // Consumes 'fd'
    ASSERT_NONNULL(dir, "");
    ASSERT_TRUE(fcheck_dir_contents(dir, bar_dir, countof(bar_dir)), "");
    ASSERT_EQ(closedir(dir), 0, "");

    // Clean up
    ASSERT_EQ(close(foo_fd), 0, "");
    ASSERT_EQ(unlink("::foo/bar/baz"), 0, "");
    ASSERT_EQ(unlink("::foo/bar"), 0, "");
    ASSERT_EQ(unlink("::foo/bit"), 0, "");
    ASSERT_EQ(unlink("::foo"), 0, "");
    END_TEST;
}

// Test cases of '..' which operate on multiple paths.
// This is mostly intended to test other pathways for client-side
// cleaning operations.
bool test_dot_dot_rename(void) {
    BEGIN_TEST;
    ASSERT_EQ(mkdir("::foo", 0755), 0, "");
    ASSERT_EQ(mkdir("::foo/bit", 0755), 0, "");
    ASSERT_EQ(mkdir("::foo/bar", 0755), 0, "");
    ASSERT_EQ(mkdir("::foo/bar/baz", 0755), 0, "");

    expected_dirent_t foo_dir_bit[] = {
        {false, ".", DT_DIR},
        {false, "..", DT_DIR},
        {false, "bar", DT_DIR},
        {false, "bit", DT_DIR},
    };

    expected_dirent_t foo_dir_bits[] = {
        {false, ".", DT_DIR},
        {false, "..", DT_DIR},
        {false, "bar", DT_DIR},
        {false, "bits", DT_DIR},
    };

    // Check that the source is cleaned
    ASSERT_EQ(rename("::foo/bar/./../bit/./../bit", "::foo/bits"), 0, "");
    ASSERT_TRUE(check_dir_contents("::foo", foo_dir_bits, countof(foo_dir_bits)), "");

    // Check that the destination is cleaned
    ASSERT_EQ(rename("::foo/bits", "::foo/bar/baz/../../././bit"), 0, "");
    ASSERT_TRUE(check_dir_contents("::foo", foo_dir_bit, countof(foo_dir_bit)), "");

    // Check that both are cleaned
    ASSERT_EQ(rename("::foo/bar/../bit/.", "::foo/bar/baz/../../././bits"), 0, "");
    ASSERT_TRUE(check_dir_contents("::foo", foo_dir_bits, countof(foo_dir_bits)), "");

    // Check that both are cleaned (including trailing '/')
    ASSERT_EQ(rename("::foo/./bar/../bits/", "::foo/bar/baz/../../././bit/.//"), 0, "");
    ASSERT_TRUE(check_dir_contents("::foo", foo_dir_bit, countof(foo_dir_bit)), "");

    // Clean up
    ASSERT_EQ(unlink("::foo/bar/baz"), 0, "");
    ASSERT_EQ(unlink("::foo/bar"), 0, "");
    ASSERT_EQ(unlink("::foo/bit"), 0, "");
    ASSERT_EQ(unlink("::foo"), 0, "");
    END_TEST;
}

// TODO(smklein): Restrict access in ThinFS

RUN_FOR_ALL_FILESYSTEMS(dot_dot_tests,
    RUN_TEST_MEDIUM(test_dot_dot_client)
    RUN_TEST_MEDIUM(test_dot_dot_server)
    RUN_TEST_MEDIUM(test_dot_dot_rename)
)
