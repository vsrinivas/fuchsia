// Copyright 2018 The Fuchsia Authors. All rights reserved.
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

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <zircon/syscalls.h>

#include "filesystems.h"
#include "misc.h"

namespace {

// Test that zero length read and write operations are valid.
bool TestZeroLengthOperations(void) {
    BEGIN_TEST;

    const char* filename = "::zero_length_ops";
    fbl::unique_fd fd(open(filename, O_RDWR | O_CREAT, 0644));
    ASSERT_TRUE(fd);

    // Zero-length write.
    ASSERT_EQ(write(fd.get(), NULL, 0), 0);
    ASSERT_EQ(pwrite(fd.get(), NULL, 0, 0), 0);

    // Zero-length read.
    ASSERT_EQ(read(fd.get(), NULL, 0), 0);
    ASSERT_EQ(pread(fd.get(), NULL, 0, 0), 0);

    // Seek pointer unchanged.
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_CUR), 0);

    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(unlink(filename), 0);

    END_TEST;
}

// Test that non-zero length read_at and write_at operations are valid.
bool TestOffsetOperations(void) {
    BEGIN_TEST;

    srand(0xDEADBEEF);

    constexpr size_t kBufferSize = PAGE_SIZE;
    uint8_t expected[kBufferSize];
    for (size_t i = 0; i < fbl::count_of(expected); i++) {
        expected[i] = static_cast<uint8_t>(rand());
    }

    struct TestOption {
        size_t write_start;
        size_t read_start;
        size_t expected_read_length;
    };

    TestOption options[] = {
        {0, 0, kBufferSize},
        {0, 1, kBufferSize - 1},
        {1, 0, kBufferSize},
        {1, 1, kBufferSize},
    };

    for (const auto& opt : options) {
        const char* filename = "::offset_ops";
        fbl::unique_fd fd(open(filename, O_RDWR | O_CREAT, 0644));
        ASSERT_TRUE(fd);

        uint8_t buf[kBufferSize];
        memset(buf, 0, sizeof(buf));

        // 1) Write "kBufferSize" bytes at opt.write_start
        ASSERT_EQ(pwrite(fd.get(), expected, sizeof(expected), opt.write_start),
                  sizeof(expected));

        // 2) Read "kBufferSize" bytes at opt.read_start;
        //    actually read opt.expected_read_length bytes.
        ASSERT_EQ(pread(fd.get(), buf, sizeof(expected), opt.read_start),
                  static_cast<ssize_t>(opt.expected_read_length));

        // 3) Verify the contents of the read matched, the seek
        //    pointer is unchanged, and the file size is correct.
        if (opt.write_start <= opt.read_start) {
            size_t read_skip = opt.read_start - opt.write_start;
            ASSERT_EQ(memcmp(buf, expected + read_skip, opt.expected_read_length), 0);
        } else {
            size_t write_skip = opt.write_start - opt.read_start;
            uint8_t zeroes[write_skip];
            memset(zeroes, 0, sizeof(zeroes));
            ASSERT_EQ(memcmp(buf, zeroes, write_skip), 0);
        }
        ASSERT_EQ(lseek(fd.get(), 0, SEEK_CUR), 0);
        struct stat st;
        ASSERT_EQ(fstat(fd.get(), &st), 0);
        ASSERT_EQ(st.st_size, static_cast<ssize_t>(opt.write_start + sizeof(expected)));

        ASSERT_EQ(close(fd.release()), 0);
        ASSERT_EQ(unlink(filename), 0);
    }

    END_TEST;
}

}  // namespace

RUN_FOR_ALL_FILESYSTEMS(rw_tests,
    RUN_TEST_MEDIUM(TestZeroLengthOperations)
    RUN_TEST_MEDIUM(TestOffsetOperations)
)
