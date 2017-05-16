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

#include <magenta/syscalls.h>
#include <mxalloc/new.h>
#include <mxtl/unique_ptr.h>

#include "filesystems.h"
#include "misc.h"

bool check_file_contains(const char* filename, const void* data, ssize_t len) {
    char buf[4096];
    struct stat st;

    ASSERT_EQ(stat(filename, &st), 0, "");
    ASSERT_EQ(st.st_size, len, "");
    int fd = open(filename, O_RDWR, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_STREAM_ALL(read, fd, buf, len);
    ASSERT_EQ(memcmp(buf, data, len), 0, "");
    ASSERT_EQ(close(fd), 0, "");

    return true;
}

bool check_file_empty(const char* filename) {
    struct stat st;
    ASSERT_EQ(stat(filename, &st), 0, "");
    ASSERT_EQ(st.st_size, 0, "");

    return true;
}

// Test that the really simple cases of truncate are operational
bool test_truncate_small(void) {
    BEGIN_TEST;

    const char* str = "Hello, World!\n";
    const char* filename = "::alpha";

    // Try writing a string to a file
    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_STREAM_ALL(write, fd, str, strlen(str));
    ASSERT_TRUE(check_file_contains(filename, str, strlen(str)), "");

    // Check that opening a file with O_TRUNC makes it empty
    int fd2 = open(filename, O_RDWR | O_TRUNC, 0644);
    ASSERT_GT(fd2, 0, "");
    ASSERT_TRUE(check_file_empty(filename), "");

    // Check that we can still write to a file that has been truncated
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_STREAM_ALL(write, fd, str, strlen(str));
    ASSERT_TRUE(check_file_contains(filename, str, strlen(str)), "");

    // Check that we can truncate the file using the "truncate" function
    ASSERT_EQ(truncate(filename, 5), 0, "");
    ASSERT_TRUE(check_file_contains(filename, str, 5), "");
    ASSERT_EQ(truncate(filename, 0), 0, "");
    ASSERT_TRUE(check_file_empty(filename), "");

    // Check that truncating an already empty file does not cause problems
    ASSERT_EQ(truncate(filename, 0), 0, "");
    ASSERT_TRUE(check_file_empty(filename), "");

    // Check that we can use truncate to extend a file
    char empty[5] = {0, 0, 0, 0, 0};
    ASSERT_EQ(truncate(filename, 5), 0, "");
    ASSERT_TRUE(check_file_contains(filename, empty, 5), "");

    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(close(fd2), 0, "");
    ASSERT_EQ(unlink(filename), 0, "");

    END_TEST;
}

template <bool Remount>
bool checked_truncate(const char* filename, uint8_t* u8, ssize_t new_len) {
    // Acquire the old size
    struct stat st;
    ASSERT_EQ(stat(filename, &st), 0, "");
    ssize_t old_len = st.st_size;

    // Truncate the file, verify the size gets updated
    int fd = open(filename, O_RDWR, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(ftruncate(fd, new_len), 0, "");
    ASSERT_EQ(stat(filename, &st), 0, "");
    ASSERT_EQ(st.st_size, new_len, "");

    // Close and reopen the file; verify the inode stays updated
    ASSERT_EQ(close(fd), 0, "");
    fd = open(filename, O_RDWR, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(stat(filename, &st), 0, "");
    ASSERT_EQ(st.st_size, new_len, "");

    if (Remount) {
        ASSERT_EQ(close(fd), 0, "");
        ASSERT_TRUE(check_remount(), "Could not remount filesystem");
        ASSERT_EQ(stat(filename, &st), 0, "");
        ASSERT_EQ(st.st_size, new_len, "");
        fd = open(filename, O_RDWR, 0644);
    }

    AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> readbuf(new (&ac) uint8_t[new_len]);
    ASSERT_TRUE(ac.check(), "");
    if (new_len > old_len) { // Expanded the file
        // Verify that the file is unchanged up to old_len
        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
        ASSERT_STREAM_ALL(read, fd, readbuf.get(), old_len);
        ASSERT_EQ(memcmp(readbuf.get(), u8, old_len), 0, "");
        // Verify that the file is filled with zeroes from old_len to new_len
        ASSERT_EQ(lseek(fd, old_len, SEEK_SET), old_len, "");
        ASSERT_STREAM_ALL(read, fd, readbuf.get(), new_len - old_len);
        for (ssize_t n = 0; n < (new_len - old_len); n++) {
            ASSERT_EQ(readbuf[n], 0, "");
        }
        // Overwrite those zeroes with the contents of u8
        ASSERT_EQ(lseek(fd, old_len, SEEK_SET), old_len, "");
        ASSERT_STREAM_ALL(write, fd, u8 + old_len, new_len - old_len);
    } else { // Shrunk the file (or kept it the same length)
        // Verify that the file is unchanged up to new_len
        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
        ASSERT_STREAM_ALL(read, fd, readbuf.get(), new_len);
        ASSERT_EQ(memcmp(readbuf.get(), u8, new_len), 0, "");
    }
    ASSERT_EQ(close(fd), 0, "");

    return true;
}

// Test that truncate doesn't have issues dealing with larger files
// Repeatedly write to / truncate a file.
template <size_t BufSize, size_t Iterations, bool Remount>
bool test_truncate_large(void) {
    if (Remount && !test_info->can_be_mounted) {
        fprintf(stderr, "Filesystem cannot be mounted; cannot test persistence\n");
        return true;
    }

    BEGIN_TEST;

    // Fill a test buffer with data
    AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[BufSize]);
    ASSERT_TRUE(ac.check(), "");

    unsigned seed = static_cast<unsigned>(mx_ticks_get());
    unittest_printf("Truncate test using seed: %u\n", seed);
    srand(seed);
    for (unsigned n = 0; n < BufSize; n++) {
        buf[n] = static_cast<uint8_t>(rand_r(&seed));
    }

    // Start a file filled with the u8 buffer
    const char* filename = "::alpha";
    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_STREAM_ALL(write, fd, buf.get(), BufSize);
    ASSERT_EQ(close(fd), 0, "");

    // Repeatedly truncate / write to the file
    for (size_t i = 0; i < Iterations; i++) {
        size_t len = rand_r(&seed) % BufSize;
        ASSERT_TRUE(checked_truncate<Remount>(filename, buf.get(), len), "");
    }
    ASSERT_EQ(unlink(filename), 0, "");

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(truncate_tests,
    RUN_TEST_MEDIUM(test_truncate_small)
    RUN_TEST_MEDIUM((test_truncate_large<1 << 10, 100, false>))
    RUN_TEST_MEDIUM((test_truncate_large<1 << 15, 50, false>))
    RUN_TEST_LARGE((test_truncate_large<1 << 20, 50, false>))
    RUN_TEST_LARGE((test_truncate_large<1 << 20, 50, true>))
)
