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

// TODO(smklein): Test direntry coalescing (fragment directory, unlink files,
// attempt to allocate a larger direntry).

int test_directory(void) {
    test_directory_filename_max();
    test_directory_large();
    return 0;
}
