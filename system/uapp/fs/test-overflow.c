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

void test_overflow_name(void) {
    fprintf(stderr, "Test Overflow (name)\n");

    char name_largest[PATH_MAX];
    char name_largest_alt[PATH_MAX];
    char name_too_large[PATH_MAX];
    make_name(name_largest, NAME_MAX, 'a');
    make_name(name_largest_alt, NAME_MAX, 'b');
    make_name(name_too_large, NAME_MAX + 1, 'a');

    // Try opening, closing, renaming, and unlinking the largest acceptable name
    int fd = TRY(open(name_largest, O_RDWR | O_CREAT | O_EXCL, 0644));
    TRY(close(fd));
    TRY(rename(name_largest, name_largest_alt));
    TRY(rename(name_largest_alt, name_largest));
    fprintf(stderr, "    (1 / 5) Name overflow: Accessed Largest Filename\n");
    EXPECT_FAIL(rename(name_largest, name_too_large));
    EXPECT_FAIL(rename(name_too_large, name_largest));
    TRY(unlink(name_largest));
    fprintf(stderr, "    (2 / 5) Name overflow: Unlinked Largest Filename\n");

    // Try it with a directory too
    TRY(mkdir(name_largest, 0755));
    TRY(rename(name_largest, name_largest_alt));
    TRY(rename(name_largest_alt, name_largest));
    fprintf(stderr, "    (3 / 5) Name overflow: Accessed Largest Dirname\n");
    EXPECT_FAIL(rename(name_largest, name_too_large));
    EXPECT_FAIL(rename(name_too_large, name_largest));
    TRY(unlink(name_largest));
    fprintf(stderr, "    (4 / 5) Name overflow: Unlinked Largest Dirname\n");

    // Try opening an unacceptably large name
    EXPECT_FAIL(open(name_too_large, O_RDWR | O_CREAT | O_EXCL, 0644));
    // Try it with a directory too
    EXPECT_FAIL(mkdir(name_too_large, 0755));
    fprintf(stderr, "    (5 / 5) Name overflow: Tried opening 'too large' names\n");
}

void test_overflow_path(void) {
    fprintf(stderr, "Test Overflow (path)\n");
    // Make the name buffer larger than PATH_MAX so we don't overflow
    char name[2 * PATH_MAX];

    int depth = 0;

    // Create an initial directory
    make_name(name, NAME_MAX, 'a');
    TRY(mkdir(name, 0755));
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

    fprintf(stderr, "    (1 / 2) Path overflow: Reached PATH_MAX.\n");

    // Remove all child directories
    while (depth != 0) {
        char* last_slash = strrchr(name, '/');
        assert(last_slash != NULL);
        assert(*last_slash == '/');
        *last_slash = '\0';
        TRY(unlink(name));
        depth--;
    }

    fprintf(stderr, "    (2 / 2) Path overflow: Finished deleting directories.\n");
}

void test_overflow_integer(void) {
    fprintf(stderr, "Test Overflow (integer)\n");
    int fd = TRY(open("::file", O_CREAT | O_RDWR | O_EXCL, 0644));

    // TODO(smklein): Test extremely large reads/writes when remoteio can handle them without
    // crashing
    /*
    char buf[4096];
    EXPECT_FAIL(write(fd, buf, SIZE_MAX - 1));
    EXPECT_FAIL(write(fd, buf, SIZE_MAX));

    EXPECT_FAIL(read(fd, buf, SIZE_MAX - 1));
    EXPECT_FAIL(read(fd, buf, SIZE_MAX));
    */

    EXPECT_FAIL(ftruncate(fd, INT_MIN));
    EXPECT_FAIL(ftruncate(fd, -1));
    EXPECT_FAIL(ftruncate(fd, SIZE_MAX - 1));
    EXPECT_FAIL(ftruncate(fd, SIZE_MAX));

    EXPECT_FAIL(lseek(fd, INT_MIN, SEEK_SET));
    EXPECT_FAIL(lseek(fd, -1, SEEK_SET));
    EXPECT_FAIL(lseek(fd, SIZE_MAX - 1, SEEK_SET));
    EXPECT_FAIL(lseek(fd, SIZE_MAX, SEEK_SET));
    close(fd);
    TRY(unlink("::file"));
}

int test_overflow(void) {
    test_overflow_name();
    test_overflow_path();
    test_overflow_integer();
    return 0;
}
