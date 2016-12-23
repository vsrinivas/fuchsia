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

// Make some files, then unlink them.
void test_unlink_simple(void) {
    printf("Test Unlink (simple)\n");
    const char* const paths[] = {"::abc", "::def", "::ghi", "::jkl", "::mnopqrstuvxyz"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        int fd = TRY(open(paths[i], O_RDWR | O_CREAT | O_EXCL, 0644));
        TRY(close(fd));
    }
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        TRY(unlink(paths[i]));
    }
}

const char* const STRING_DATA[] = {
    "Hello, world",
    "Foo bar baz blat",
    "This is yet another sample string",
};

static void simple_read_test(int fd, size_t data_index) {
    assert(lseek(fd, 0, SEEK_SET) == 0);
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    ssize_t len = strlen(STRING_DATA[data_index]);
    assert(read(fd, buf, len) == len);
    assert(memcmp(STRING_DATA[data_index], buf, strlen(STRING_DATA[data_index])) == 0);
}

static void simple_write_test(int fd, size_t data_index) {
    TRY(ftruncate(fd, 0));
    assert(lseek(fd, 0, SEEK_SET) == 0);
    ssize_t len = strlen(STRING_DATA[data_index]);
    assert(write(fd, STRING_DATA[data_index], len) == len);
    simple_read_test(fd, data_index);
}

void test_unlink_use_afterwards(void) {
    printf("Test Unlink (use afterwards)\n");
    const char* path = "::foobar";
    int fd = TRY(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));

    simple_write_test(fd, 1);

    // When we unlink path, fd is still open.
    TRY(unlink(path));
    simple_read_test(fd, 1);  // It should contain the same data as before
    simple_write_test(fd, 2); // It should still be writable
    TRY(close(fd));           // This actually releases the file

    // Now, opening the file should fail without O_CREAT
    EXPECT_FAIL(open(path, O_RDWR, 0644));
}

void test_unlink_open_elsewhere(void) {
    printf("Test Unlink (open elsewhere)\n");
    const char* path = "::foobar";
    int fd1 = TRY(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
    int fd2 = TRY(open(path, O_RDWR, 0644));

    simple_write_test(fd1, 0);
    TRY(close(fd1));

    // When we unlink path, fd2 is still open.
    TRY(unlink(path));
    simple_read_test(fd2, 0);  // It should contain the same data as before
    simple_write_test(fd2, 1); // It should still be writable
    TRY(close(fd2));           // This actually releases the file

    // Now, opening the file should fail without O_CREAT
    EXPECT_FAIL(open(path, O_RDWR, 0644));
}

int test_unlink(void) {
    test_unlink_simple();
    test_unlink_use_afterwards();
    test_unlink_open_elsewhere();
    return 0;
}
