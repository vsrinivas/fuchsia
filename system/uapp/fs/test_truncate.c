// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

// Test that the really simple cases of truncate are operational
void test_truncate_small(void) {
    printf("Test Truncate (small)\n");
    const char* str = "Hello, World!\n";
    const char* filename = "::alpha";

    // Try writing a string to a file
    int fd = TRY(open(filename, O_RDWR | O_CREAT, 0644));
    TRY(write(fd, str, strlen(str)));
    check_file_contains(filename, str, strlen(str));

    // Check that opening a file with O_TRUNC makes it empty
    int fd2 = TRY(open(filename, O_RDWR | O_TRUNC, 0644));
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
    TRY(unlink(filename));
}

#define BUFSIZE 1048576
static_assert(BUFSIZE == ((BUFSIZE / sizeof(uint64_t)) * sizeof(uint64_t)),
              "BUFSIZE not multiple of sizeof(uint64_t)");

void checked_truncate(const char* filename, uint8_t* u8, ssize_t new_len) {
    // Acquire the old size
    struct stat st;
    TRY(stat(filename, &st));
    ssize_t old_len = st.st_size;

    // Truncate the file
    int fd = TRY(open(filename, O_RDWR, 0644));
    TRY(ftruncate(fd, new_len));

    // Verify that the size has been updated
    TRY(stat(filename, &st));
    assert(st.st_size == new_len);

    ssize_t r;
    uint8_t *readbuf = malloc(BUFSIZE);
    assert(readbuf != NULL);
    if (new_len > old_len) { // Expanded the file
        // Verify that the file is unchanged up to old_len
        TRY(lseek(fd, 0, SEEK_SET));
        TRY((r = read(fd, readbuf, old_len)));
        assert(r == old_len);
        assert(memcmp(readbuf, u8, old_len) == 0);
        // Verify that the file is filled with zeroes from old_len to new_len
        TRY(lseek(fd, old_len, SEEK_SET));
        TRY((r = read(fd, readbuf, new_len - old_len)));
        assert(r == (new_len - old_len));
        for (unsigned n = 0; n < (new_len - old_len); n++) {
            assert(readbuf[n] == 0);
        }
        // Overwrite those zeroes with the contents of u8
        TRY(lseek(fd, old_len, SEEK_SET));
        TRY((r = write(fd, u8 + old_len, new_len - old_len)));
        assert(r = new_len - old_len);
    } else { // Shrunk the file (or kept it the same length)
        // Verify that the file is unchanged up to new_len
        TRY(lseek(fd, 0, SEEK_SET));
        TRY((r = read(fd, readbuf, new_len)));
        assert(r == new_len);
        assert(memcmp(readbuf, u8, new_len) == 0);
    }
    TRY(close(fd));
    free(readbuf);
}

// Test that truncate doesn't have issues dealing with larger files
// Repeatedly write to / truncate a file.
void test_truncate_large(void) {
    printf("Test Truncate (large)\n");
    // Fill a test buffer with data
    uint64_t *u64 = malloc(BUFSIZE);
    assert(u64 != NULL);
    rand64_t rdata;
    srand64(&rdata, "truncate_large_test");
    for (unsigned n = 0; n < (BUFSIZE / sizeof(uint64_t)); n++) {
        u64[n] = rand64(&rdata);
    }

    // Start a file filled with the u8 buffer
    const char* filename = "::alpha";
    int fd = TRY(open(filename, O_RDWR | O_CREAT, 0644));
    ssize_t r;
    TRY((r = write(fd, u64, BUFSIZE)));
    assert(r == BUFSIZE);

    // Repeatedly truncate / write to the file
    const int num_iterations = 50;
    for (int i = 0; i < num_iterations; i++) {
        size_t len = rand64(&rdata) % BUFSIZE;
        checked_truncate(filename, (uint8_t*)u64, len);
    }
    free(u64);
    TRY(close(fd));
    TRY(unlink(filename));
}

int test_truncate(void) {
    test_truncate_small();
    test_truncate_large();
    return 0;
}
