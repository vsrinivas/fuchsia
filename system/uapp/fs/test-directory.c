// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "misc.h"

void test_directory_filename_max(void) {
    printf("Test Directory (filename max)\n");

    // TODO(smklein): This value may be filesystem-specific. Plumb it through
    // from the test driver.
    int max_file_len = 255;
    char path[PATH_MAX + 1];

    // Unless the max_file_name is approaching PATH_MAX, this shouldn't be an
    // issue.
    assert(max_file_len + 3 /* '::' + '0' for 'too large' file */ < PATH_MAX);

    // Largest possible file length
    TRY(snprintf(path, sizeof(path), "::%0*d", max_file_len, 0x1337));
    int fd = TRY(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
    TRY(close(fd));
    TRY(unlink(path));

    // Slightly too large file length
    TRY(snprintf(path, sizeof(path), "::%0*d", max_file_len + 1, 0xBEEF));
    EXPECT_FAIL(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
}

// Hopefuly not pushing against any 'max file length' boundaries, but large
// enough to fill a directory quickly.
#define LARGE_PATH_LENGTH 128

void test_directory_large(void) {
    printf("Test Directory (large)\n");

    // Write a bunch of files to a directory
    const int num_files = 1024;
    for (int i = 0; i < num_files; i++) {
        char path[LARGE_PATH_LENGTH + 1];
        TRY(snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i));
        int fd = TRY(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
        TRY(close(fd));
    }

    // Unlink all those files
    for (int i = 0; i < num_files; i++) {
        char path[LARGE_PATH_LENGTH + 1];
        TRY(snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i));
        TRY(unlink(path));
    }

    // TODO(smklein): Verify contents
}

void test_directory_max(void) {
    printf("Test Directory (max)\n");

    // Write the maximum number of files to a directory
    int i = 0;
    for (;; i++) {
        char path[LARGE_PATH_LENGTH + 1];
        TRY(snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i));
        if (i % 100 == 0) {
            printf(" Allocating: %s\n", path);
        }

        int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
        if (fd < 0) {
            printf("    wrote %d direntries\n", i);
            break;
        }
        TRY(close(fd));
    }

    // Unlink all those files
    for (; i >= 0; i--) {
        char path[LARGE_PATH_LENGTH + 1];
        TRY(snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i));
        TRY(unlink(path));
    }
}

void test_directory_coalesce_helper(const int *unlink_order) {
    const char *files[] = {
        "::coalesce/aaaaaaaa",
        "::coalesce/bbbbbbbb",
        "::coalesce/cccccccc",
        "::coalesce/dddddddd",
        "::coalesce/eeeeeeee",
    };
    int num_files = sizeof(files) / sizeof(files[0]);

    // Allocate a bunch of files in a directory
    TRY(mkdir("::coalesce", 0755));
    for (int i = 0; i < num_files; i++) {
        int fd = TRY(open(files[i], O_RDWR | O_CREAT | O_EXCL, 0644));
        TRY(close(fd));
    }

    // Unlink all those files in the order specified
    for (int i = 0; i < num_files; i++) {
        assert(0 <= unlink_order[i] && unlink_order[i] < num_files);
        TRY(unlink(files[unlink_order[i]]));
    }

    TRY(unlink("::coalesce"));
}

void test_directory_coalesce(void) {
    printf("Test Directory (coalesce)\n");

    // Test some cases of coalescing, assuming the directory was filled
    // according to allocation order. If it wasn't, this test should still pass,
    // but there is no mechanism to check the "location of a direntry in a
    // directory", so this is our best shot at "poking" the filesystem to try to
    // coalesce.

    // Case 1: Test merge-with-left
    printf("  Test merge-with-left\n");
    const int merge_with_left[] = {0, 1, 2, 3, 4};
    test_directory_coalesce_helper(merge_with_left);

    // Case 2: Test merge-with-right
    printf("  Test merge-with-right\n");
    const int merge_with_right[] = {4, 3, 2, 1, 0};
    test_directory_coalesce_helper(merge_with_right);

    // Case 3: Test merge-with-both
    printf("  Test merge-with-both\n");
    const int merge_with_both[] = {1, 3, 2, 0, 4};
    test_directory_coalesce_helper(merge_with_both);
}

int test_directory(void) {
    test_directory_coalesce();
    test_directory_filename_max();
    test_directory_large();
    // TODO(smklein): Run this when MemFS can execute it without causing an OOM
#if 0
    test_directory_max();
#endif
    return 0;
}
