// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <minfs/format.h>
#include <zircon/syscalls.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>

#include "filesystems.h"

template <size_t WriteOffset, size_t ReadOffset, size_t WriteSize>
bool test_sparse(void) {
    BEGIN_TEST;

    fbl::unique_fd fd(open("::my_file", O_RDWR | O_CREAT, 0644));
    ASSERT_TRUE(fd);

    // Create a random write buffer of data
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> wbuf(new (&ac) uint8_t[WriteSize]);
    ASSERT_EQ(ac.check(), true);
    unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
    unittest_printf("Sparse test using seed: %u\n", seed);
    for (size_t i = 0; i < WriteSize; i++) {
        wbuf[i] = (uint8_t) rand_r(&seed);
    }

    // Dump write buffer to file
    ASSERT_EQ(pwrite(fd.get(), &wbuf[0], WriteSize, WriteOffset), WriteSize);

    // Reopen file
    ASSERT_EQ(close(fd.release()), 0);
    fd.reset(open("::my_file", O_RDWR, 0644));
    ASSERT_TRUE(fd);

    // Access read buffer from file
    constexpr size_t kFileSize = WriteOffset + WriteSize;
    constexpr size_t kBytesToRead = (kFileSize - ReadOffset) > WriteSize ?
                                     WriteSize : (kFileSize - ReadOffset);
    static_assert(kBytesToRead > 0, "We want to test writing AND reading");
    fbl::unique_ptr<uint8_t[]> rbuf(new (&ac) uint8_t[kBytesToRead]);
    ASSERT_EQ(ac.check(), true);
    ASSERT_EQ(pread(fd.get(), &rbuf[0], kBytesToRead, ReadOffset), kBytesToRead);

    constexpr size_t kSparseLength = (ReadOffset < WriteOffset) ?
                                      WriteOffset - ReadOffset : 0;

    if (kSparseLength > 0) {
        for (size_t i = 0; i < kSparseLength; i++) {
            ASSERT_EQ(rbuf[i], 0, "This portion of file should be sparse; but isn't");
        }
    }

    constexpr size_t kWbufOffset = (ReadOffset < WriteOffset) ?
                                    0 : ReadOffset - WriteOffset;
    constexpr size_t kValidLength = kBytesToRead - kSparseLength;

    if (kValidLength > 0) {
        for (size_t i = 0; i < kValidLength; i++) {
            ASSERT_EQ(rbuf[kSparseLength + i], wbuf[kWbufOffset + i]);
        }
    }

    // Clean up
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(unlink("::my_file"), 0);
    END_TEST;
}

bool TestSparseAllocation() {
    BEGIN_TEST;
    fbl::unique_fd sparse_fd(open("::sparse_file", O_RDWR | O_CREAT, 0644));
    ASSERT_TRUE(sparse_fd);

    char data[minfs::kMinfsBlockSize];
    memset(data, 0xaa, sizeof(data));

    // Create a file that owns blocks in |kBitmapBlocks| different bitmap blocks.
    constexpr uint32_t kBitmapBlocks = 4;
    for (uint32_t j = 0; j < kBitmapBlocks; j++) {
        // Write one block to the "sparse" file.
        ASSERT_EQ(sizeof(data), write(sparse_fd.get(), data, sizeof(data)));

        char filename[128];
        snprintf(filename, sizeof(filename), "::file_%u", j);
        fbl::unique_fd fd(open(filename, O_RDWR | O_CREAT, 0644));
        ASSERT_TRUE(fd);

        // Write enough blocks to another file to use up the remainder of a bitmap block.
        for (size_t i = 0; i < minfs::kMinfsBlockBits; i++) {
            ASSERT_EQ(sizeof(data), write(fd.get(), data, sizeof(data)));
        }
    }

    ASSERT_EQ(close(sparse_fd.release()), 0);
    ASSERT_EQ(unlink("::sparse_file"), 0);

    END_TEST;
}

constexpr size_t kBlockSize = 8192;
constexpr size_t kDirectBlocks = 16;

const test_disk_t disk = {
    .block_count = 1LLU << 24,
    .block_size = 1LLU << 9,
    .slice_size = 1LLU << 23,
};

RUN_FOR_ALL_FILESYSTEMS_SIZE(sparse_tests, disk,
    RUN_TEST_MEDIUM((test_sparse<0, 0, kBlockSize>))
    RUN_TEST_MEDIUM((test_sparse<kBlockSize / 2, 0, kBlockSize>))
    RUN_TEST_MEDIUM((test_sparse<kBlockSize / 2, kBlockSize, kBlockSize>))
    RUN_TEST_MEDIUM((test_sparse<kBlockSize, 0, kBlockSize>))
    RUN_TEST_MEDIUM((test_sparse<kBlockSize, kBlockSize / 2, kBlockSize>))

    RUN_TEST_MEDIUM((test_sparse<kBlockSize * kDirectBlocks,
                                 kBlockSize * kDirectBlocks - kBlockSize,
                                 kBlockSize * 2>))
    RUN_TEST_MEDIUM((test_sparse<kBlockSize * kDirectBlocks,
                                 kBlockSize * kDirectBlocks - kBlockSize,
                                 kBlockSize * 32>))
    RUN_TEST_MEDIUM((test_sparse<kBlockSize * kDirectBlocks + kBlockSize,
                                 kBlockSize * kDirectBlocks - kBlockSize,
                                 kBlockSize * 32>))
    RUN_TEST_MEDIUM((test_sparse<kBlockSize * kDirectBlocks + kBlockSize,
                                 kBlockSize * kDirectBlocks + 2 * kBlockSize,
                                 kBlockSize * 32>))
    RUN_TEST_LARGE(TestSparseAllocation)
)
