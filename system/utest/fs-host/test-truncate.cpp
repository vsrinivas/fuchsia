// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <time.h>

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

#include "util.h"

bool check_file_contains(const char* filename, const void* data, ssize_t len) {
    char buf[4096];
    struct stat st;

    ASSERT_EQ(emu_stat(filename, &st), 0);
    ASSERT_EQ(st.st_size, len);
    int fd = emu_open(filename, O_RDWR, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_STREAM_ALL(emu_read, fd, buf, len);
    ASSERT_EQ(memcmp(buf, data, len), 0);
    ASSERT_EQ(emu_close(fd), 0);

    return true;
}

bool check_file_empty(const char* filename) {
    struct stat st;
    ASSERT_EQ(emu_stat(filename, &st), 0);
    ASSERT_EQ(st.st_size, 0);

    return true;
}

// Test that the really simple cases of truncate are operational
bool test_truncate_small(void) {
    BEGIN_TEST;

    const char* str = "Hello, World!\n";
    const char* filename = "::alpha";

    // Try writing a string to a file
    int fd = emu_open(filename, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_STREAM_ALL(emu_write, fd, str, strlen(str));
    ASSERT_TRUE(check_file_contains(filename, str, strlen(str)));

    // Check that opening a file with O_TRUNC makes it empty
    int fd2 = emu_open(filename, O_RDWR | O_TRUNC, 0644);
    ASSERT_GT(fd2, 0);
    ASSERT_TRUE(check_file_empty(filename));

    // Check that we can still write to a file that has been truncated
    ASSERT_EQ(emu_lseek(fd, 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(emu_write, fd, str, strlen(str));
    ASSERT_TRUE(check_file_contains(filename, str, strlen(str)));

    // Check that we can truncate the file using the "truncate" function
    ASSERT_EQ(emu_ftruncate(fd, 5), 0);
    ASSERT_TRUE(check_file_contains(filename, str, 5));
    ASSERT_EQ(emu_ftruncate(fd, 0), 0);
    ASSERT_TRUE(check_file_empty(filename));

    // Check that truncating an already empty file does not cause problems
    ASSERT_EQ(emu_ftruncate(fd, 0), 0);
    ASSERT_TRUE(check_file_empty(filename));

    // Check that we can use truncate to extend a file
    char empty[5] = {0, 0, 0, 0, 0};
    ASSERT_EQ(emu_ftruncate(fd, 5), 0);
    ASSERT_TRUE(check_file_contains(filename, empty, 5));

    ASSERT_EQ(emu_close(fd), 0);
    ASSERT_EQ(emu_close(fd2), 0);

    END_TEST;
}

bool checked_truncate(const char* filename, uint8_t* u8, ssize_t new_len) {
    BEGIN_HELPER;
    // Acquire the old size
    struct stat st;
    ASSERT_EQ(emu_stat(filename, &st), 0);
    ssize_t old_len = st.st_size;

    // Truncate the file, verify the size gets updated
    int fd = emu_open(filename, O_RDWR, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(emu_ftruncate(fd, new_len), 0);
    ASSERT_EQ(emu_stat(filename, &st), 0);
    ASSERT_EQ(st.st_size, new_len);

    // close and reopen the file; verify the inode stays updated
    ASSERT_EQ(emu_close(fd), 0);
    fd = emu_open(filename, O_RDWR, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(emu_stat(filename, &st), 0);
    ASSERT_EQ(st.st_size, new_len);

    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> readbuf(new (&ac) uint8_t[new_len]);
    ASSERT_TRUE(ac.check());
    if (new_len > old_len) { // Expanded the file
        // Verify that the file is unchanged up to old_len
        ASSERT_EQ(emu_lseek(fd, 0, SEEK_SET), 0);
        ASSERT_STREAM_ALL(emu_read, fd, readbuf.get(), old_len);
        ASSERT_EQ(memcmp(readbuf.get(), u8, old_len), 0);
        // Verify that the file is filled with zeroes from old_len to new_len
        ASSERT_EQ(emu_lseek(fd, old_len, SEEK_SET), old_len);
        ASSERT_STREAM_ALL(emu_read, fd, readbuf.get(), new_len - old_len);
        for (ssize_t n = 0; n < (new_len - old_len); n++) {
            ASSERT_EQ(readbuf[n], 0);
        }
        // Overwrite those zeroes with the contents of u8
        ASSERT_EQ(emu_lseek(fd, old_len, SEEK_SET), old_len);
        ASSERT_STREAM_ALL(emu_write, fd, u8 + old_len, new_len - old_len);
    } else { // Shrunk the file (or kept it the same length)
        // Verify that the file is unchanged up to new_len
        ASSERT_EQ(emu_lseek(fd, 0, SEEK_SET), 0);
        ASSERT_STREAM_ALL(emu_read, fd, readbuf.get(), new_len);
        ASSERT_EQ(memcmp(readbuf.get(), u8, new_len), 0);
    }

    ASSERT_EQ(emu_close(fd), 0);
    ASSERT_EQ(run_fsck(), 0);
    END_HELPER;
}

// Test that truncate doesn't have issues dealing with larger files
// Repeatedly write to / truncate a file.
template <size_t BufSize, size_t Iterations>
bool test_truncate_large(void) {
    BEGIN_TEST;

    // Fill a test buffer with data
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[BufSize]);
    ASSERT_TRUE(ac.check());

    unsigned seed = static_cast<unsigned>(time(0));
    unittest_printf("Truncate test using seed: %u\n", seed);
    srand(seed);
    for (unsigned n = 0; n < BufSize; n++) {
        buf[n] = static_cast<uint8_t>(rand_r(&seed));
    }

    // Start a file filled with the u8 buffer
    const char* filename = "::alpha";
    int fd = emu_open(filename, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_STREAM_ALL(emu_write, fd, buf.get(), BufSize);
    ASSERT_EQ(emu_close(fd), 0);

    // Repeatedly truncate / write to the file
    for (size_t i = 0; i < Iterations; i++) {
        size_t len = rand_r(&seed) % BufSize;
        ASSERT_TRUE(checked_truncate(filename, buf.get(), len));
    }

    END_TEST;
}

RUN_MINFS_TESTS(truncate_tests,
    RUN_TEST_MEDIUM(test_truncate_small)
    RUN_TEST_MEDIUM((test_truncate_large<1 << 10, 1000>))
    RUN_TEST_MEDIUM((test_truncate_large<1 << 15, 500>))
    RUN_TEST_LARGE((test_truncate_large<1 << 20, 500>))
    RUN_TEST_LARGE((test_truncate_large<1 << 25, 500>))
)
