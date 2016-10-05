// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "misc.h"

void check_file_contains(const char* filename, const void* data, ssize_t len) {
    char buf[4096];
    struct stat st;

    TRY(stat(filename, &st));
    assert(st.st_size == len);
    int fd = TRY(open(filename, O_RDWR, 0644));
    ssize_t r;
    TRY((r = read(fd, buf, len)));
    assert(r == len);
    assert(memcmp(buf, data, len) == 0);
    TRY(close(fd));
}

void check_file_empty(const char* filename) {
    struct stat st;
    TRY(stat(filename, &st));
    assert(st.st_size == 0);
}

int test_truncate(void) {
    const char* str = "Hello, World!\n";
    const char* filename = "::alpha";

    // Try writing a string to a file
    int fd = TRY(open(filename, O_RDWR|O_CREAT, 0644));
    TRY(write(fd, str, strlen(str)));
    check_file_contains(filename, str, strlen(str));

    // Check that opening a file with O_TRUNC makes it empty
    int fd2 = TRY(open(filename, O_RDWR|O_TRUNC, 0644));
    check_file_empty(filename);

    // Check that we can still write to a file that has been truncated
    TRY(lseek(fd, 0, SEEK_SET));
    TRY(write(fd, str, strlen(str)));
    check_file_contains(filename, str, strlen(str));

    // Check that we can truncate the file using the "truncate" function
    TRY(truncate(filename, 5));
    check_file_contains(filename, str, 5);
    TRY(truncate(filename, 0));
    check_file_empty(filename);

    // Check that truncating an already empty file does not cause problems
    TRY(truncate(filename, 0));
    check_file_empty(filename);

    // Check that we can use truncate to extend a file
    char empty[5] = {0, 0, 0, 0, 0};
    TRY(truncate(filename, 5));
    check_file_contains(filename, empty, 5);

    TRY(close(fd));
    TRY(close(fd2));
    return 0;
}
