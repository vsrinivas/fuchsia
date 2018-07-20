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

#include <zircon/syscalls.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

#include "filesystems.h"
#include "misc.h"

namespace {

bool check_file_contains(const char* filename, const void* data, ssize_t len) {
    char buf[4096];
    struct stat st;

    ASSERT_EQ(stat(filename, &st), 0);
    ASSERT_EQ(st.st_size, len);
    int fd = open(filename, O_RDWR, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_STREAM_ALL(read, fd, buf, len);
    ASSERT_EQ(memcmp(buf, data, len), 0);
    ASSERT_EQ(close(fd), 0);

    return true;
}

bool check_file_empty(const char* filename) {
    struct stat st;
    ASSERT_EQ(stat(filename, &st), 0);
    ASSERT_EQ(st.st_size, 0);

    return true;
}

// Test that the really simple cases of truncate are operational
bool test_truncate_small(void) {
    BEGIN_TEST;

    const char* str = "Hello, World!\n";
    const char* filename = "::alpha";

    // Try writing a string to a file
    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_STREAM_ALL(write, fd, str, strlen(str));
    ASSERT_TRUE(check_file_contains(filename, str, strlen(str)));

    // Check that opening a file with O_TRUNC makes it empty
    int fd2 = open(filename, O_RDWR | O_TRUNC, 0644);
    ASSERT_GT(fd2, 0);
    ASSERT_TRUE(check_file_empty(filename));

    // Check that we can still write to a file that has been truncated
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(write, fd, str, strlen(str));
    ASSERT_TRUE(check_file_contains(filename, str, strlen(str)));

    // Check that we can truncate the file using the "truncate" function
    ASSERT_EQ(truncate(filename, 5), 0);
    ASSERT_TRUE(check_file_contains(filename, str, 5));
    ASSERT_EQ(truncate(filename, 0), 0);
    ASSERT_TRUE(check_file_empty(filename));

    // Check that truncating an already empty file does not cause problems
    ASSERT_EQ(truncate(filename, 0), 0);
    ASSERT_TRUE(check_file_empty(filename));

    // Check that we can use truncate to extend a file
    char empty[5] = {0, 0, 0, 0, 0};
    ASSERT_EQ(truncate(filename, 5), 0);
    ASSERT_TRUE(check_file_contains(filename, empty, 5));

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(fd2), 0);
    ASSERT_EQ(unlink(filename), 0);

    END_TEST;
}

bool fill_file(int fd, uint8_t* u8, ssize_t new_len, ssize_t old_len) {
    BEGIN_HELPER;
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> readbuf(new (&ac) uint8_t[new_len]);
    ASSERT_TRUE(ac.check());
    if (new_len > old_len) { // Expanded the file
        // Verify that the file is unchanged up to old_len
        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
        ASSERT_STREAM_ALL(read, fd, readbuf.get(), old_len);
        ASSERT_EQ(memcmp(readbuf.get(), u8, old_len), 0);
        // Verify that the file is filled with zeroes from old_len to new_len
        ASSERT_EQ(lseek(fd, old_len, SEEK_SET), old_len);
        ASSERT_STREAM_ALL(read, fd, readbuf.get(), new_len - old_len);
        for (ssize_t n = 0; n < (new_len - old_len); n++) {
            ASSERT_EQ(readbuf[n], 0);
        }
        // Overwrite those zeroes with the contents of u8
        ASSERT_EQ(lseek(fd, old_len, SEEK_SET), old_len);
        ASSERT_STREAM_ALL(write, fd, u8 + old_len, new_len - old_len);
    } else { // Shrunk the file (or kept it the same length)
        // Verify that the file is unchanged up to new_len
        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
        ASSERT_STREAM_ALL(read, fd, readbuf.get(), new_len);
        ASSERT_EQ(memcmp(readbuf.get(), u8, new_len), 0);
    }
    END_HELPER;
}

template <bool Remount>
bool checked_truncate(const char* filename, uint8_t* u8, ssize_t new_len) {
    BEGIN_HELPER;
    // Acquire the old size
    struct stat st;
    ASSERT_EQ(stat(filename, &st), 0);
    ssize_t old_len = st.st_size;

    // Truncate the file, verify the size gets updated
    int fd = open(filename, O_RDWR, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(ftruncate(fd, new_len), 0);
    ASSERT_EQ(stat(filename, &st), 0);
    ASSERT_EQ(st.st_size, new_len);

    // Close and reopen the file; verify the inode stays updated
    ASSERT_EQ(close(fd), 0);
    fd = open(filename, O_RDWR, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(stat(filename, &st), 0);
    ASSERT_EQ(st.st_size, new_len);

    if (Remount) {
        ASSERT_EQ(close(fd), 0);
        ASSERT_TRUE(check_remount(), "Could not remount filesystem");
        ASSERT_EQ(stat(filename, &st), 0);
        ASSERT_EQ(st.st_size, new_len);
        fd = open(filename, O_RDWR, 0644);
    }

    ASSERT_TRUE(fill_file(fd, u8, new_len, old_len));
    ASSERT_EQ(close(fd), 0);
    END_HELPER;
}

bool fchecked_truncate(int fd, uint8_t* u8, ssize_t new_len) {
    BEGIN_HELPER;

    // Acquire the old size
    struct stat st;
    ASSERT_EQ(fstat(fd, &st), 0);
    ssize_t old_len = st.st_size;

    // Truncate the file, verify the size gets updated
    ASSERT_EQ(ftruncate(fd, new_len), 0);
    ASSERT_EQ(fstat(fd, &st), 0);
    ASSERT_EQ(st.st_size, new_len);

    ASSERT_TRUE(fill_file(fd, u8, new_len, old_len));
    END_HELPER;
}

enum TestType {
    KeepOpen,
    Reopen,
    Remount,
};

// Test that truncate doesn't have issues dealing with larger files
// Repeatedly write to / truncate a file.
template <size_t BufSize, size_t Iterations, TestType Test>
bool test_truncate_large(void) {
    BEGIN_TEST;

    if ((Test == Remount) && !test_info->can_be_mounted) {
        fprintf(stderr, "Filesystem cannot be mounted; cannot test persistence\n");
        return true;
    }

    // Fill a test buffer with data
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[BufSize]);
    ASSERT_TRUE(ac.check());

    unsigned seed = static_cast<unsigned>(zx_ticks_get());
    unittest_printf("Truncate test using seed: %u\n", seed);
    srand(seed);
    for (unsigned n = 0; n < BufSize; n++) {
        buf[n] = static_cast<uint8_t>(rand_r(&seed));
    }

    // Start a file filled with a buffer
    const char* filename = "::alpha";
    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_STREAM_ALL(write, fd, buf.get(), BufSize);

    if (Test != KeepOpen) {
        ASSERT_EQ(close(fd), 0);
    }

    // Repeatedly truncate / write to the file
    for (size_t i = 0; i < Iterations; i++) {
        size_t len = rand_r(&seed) % BufSize;
        if (Test == KeepOpen) {
            ASSERT_TRUE(fchecked_truncate(fd, buf.get(), len));
        } else {
            ASSERT_TRUE(checked_truncate<Test == Remount>(filename, buf.get(), len));
        }
    }
    ASSERT_EQ(unlink(filename), 0);
    if (Test == KeepOpen) {
        ASSERT_EQ(close(fd), 0);
    }

    END_TEST;
}

enum SparseTestType {
    UnlinkThenClose,
    CloseThenUnlink,
};

// This test catches a particular regression in MinFS truncation, where,
// if a block is cut in half for truncation, it is read, filled with
// zeroes, and writen back out to disk.
//
// This test tries to proke at a variety of offsets of interest.
template <SparseTestType Test>
bool test_truncate_partial_block_sparse(void) {
    BEGIN_TEST;

    if (strcmp(test_info->name, "minfs")) {
        fprintf(stderr, "Test is MinFS-Exclusive; ignoring\n");
        return true;
    }

    // TODO(smklein): Acquire these constants directly from MinFS's header
    constexpr size_t kBlockSize = 8192;
    constexpr size_t kDirectBlocks = 16;
    constexpr size_t kIndirectBlocks = 31;
    constexpr size_t kDirectPerIndirect = kBlockSize / 4;

    uint8_t buf[kBlockSize];
    memset(buf, 0xAB, sizeof(buf));

    off_t write_offsets[] = {
        kBlockSize * 5,
        kBlockSize * kDirectBlocks,
        kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * 1,
        kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * 2,
        kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks - 2 * kBlockSize,
        kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks - kBlockSize,
        kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks,
        kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks + kBlockSize,
    };

    for (size_t i = 0; i < fbl::count_of(write_offsets); i++) {
        off_t write_off = write_offsets[i];
        int fd = open("::truncate-sparse", O_CREAT | O_RDWR);
        ASSERT_GT(fd, 0);
        ASSERT_EQ(lseek(fd, write_off, SEEK_SET), write_off);
        ASSERT_EQ(write(fd, buf, sizeof(buf)), sizeof(buf));
        ASSERT_EQ(ftruncate(fd, write_off + 2 * kBlockSize), 0);
        ASSERT_EQ(ftruncate(fd, write_off + kBlockSize + kBlockSize / 2), 0);
        ASSERT_EQ(ftruncate(fd, write_off + kBlockSize / 2), 0);
        ASSERT_EQ(ftruncate(fd, write_off - kBlockSize / 2), 0);
        if (Test == UnlinkThenClose) {
            ASSERT_EQ(unlink("::truncate-sparse"), 0);
            ASSERT_EQ(close(fd), 0);
        } else {
            ASSERT_EQ(close(fd), 0);
            ASSERT_EQ(unlink("::truncate-sparse"), 0);
        }
    }

    END_TEST;
}

bool test_truncate_errno(void) {
    BEGIN_TEST;

    int fd = open("::truncate_errno", O_RDWR | O_CREAT | O_EXCL);
    ASSERT_GT(fd, 0);

    ASSERT_EQ(ftruncate(fd, -1), -1);
    ASSERT_EQ(errno, EINVAL);
    errno = 0;
    ASSERT_EQ(ftruncate(fd, 1UL << 60), -1);
    ASSERT_EQ(errno, EINVAL);

    ASSERT_EQ(unlink("::truncate_errno"), 0);
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

const test_disk_t disk = {
    .block_count = 3 * (1LLU << 16),
    .block_size = 1LLU << 9,
    .slice_size = 1LLU << 23,
};

}  // namespace

RUN_FOR_ALL_FILESYSTEMS_SIZE(truncate_tests, disk,
    RUN_TEST_MEDIUM(test_truncate_small)
    RUN_TEST_MEDIUM((test_truncate_large<1 << 10, 100, KeepOpen>))
    RUN_TEST_MEDIUM((test_truncate_large<1 << 10, 100, Reopen>))
    RUN_TEST_MEDIUM((test_truncate_large<1 << 15, 50, KeepOpen>))
    RUN_TEST_MEDIUM((test_truncate_large<1 << 15, 50, Reopen>))
    RUN_TEST_LARGE((test_truncate_large<1 << 20, 50, KeepOpen>))
    RUN_TEST_LARGE((test_truncate_large<1 << 20, 50, Reopen>))
    RUN_TEST_LARGE((test_truncate_large<1 << 20, 50, Remount>))
    RUN_TEST_LARGE((test_truncate_large<1 << 25, 50, KeepOpen>))
    RUN_TEST_LARGE((test_truncate_large<1 << 25, 50, Reopen>))
    RUN_TEST_LARGE((test_truncate_large<1 << 25, 50, Remount>))
    RUN_TEST_MEDIUM((test_truncate_partial_block_sparse<UnlinkThenClose>))
    RUN_TEST_MEDIUM((test_truncate_partial_block_sparse<CloseThenUnlink>))
    RUN_TEST_MEDIUM(test_truncate_errno)
)
