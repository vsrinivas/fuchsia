// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <time.h>

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

#include "util.h"

static unsigned count = 0;
template <size_t WriteOffset, size_t ReadOffset, size_t WriteSize>
bool test_sparse(void) {
    BEGIN_TEST;

    char filename[20];
    sprintf(filename, "::my_file_%u", count++);

    int fd = emu_open(filename, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);

    // Create a random write buffer of data
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> wbuf(new (&ac) uint8_t[WriteSize]);
    ASSERT_EQ(ac.check(), true);
    unsigned int seed = static_cast<unsigned int>(time(NULL));
    unittest_printf("Sparse test using seed: %u\n", seed);
    for (size_t i = 0; i < WriteSize; i++) {
        wbuf[i] = (uint8_t) rand_r(&seed);
    }

    // Dump write buffer to file
    ASSERT_EQ(emu_pwrite(fd, &wbuf[0], WriteSize, WriteOffset), WriteSize);
    // Reopen file
    ASSERT_EQ(emu_close(fd), 0);
    fd = emu_open(filename, O_RDWR, 0644);
    ASSERT_GT(fd, 0);

    // Access read buffer from file
    constexpr size_t kFileSize = WriteOffset + WriteSize;
    constexpr size_t kBytesToRead = (kFileSize - ReadOffset) > WriteSize ?
                                     WriteSize : (kFileSize - ReadOffset);
    static_assert(kBytesToRead > 0, "We want to test writing AND reading");
    fbl::unique_ptr<uint8_t[]> rbuf(new (&ac) uint8_t[kBytesToRead]);
    ASSERT_EQ(ac.check(), true);
    ASSERT_EQ(emu_pread(fd, &rbuf[0], kBytesToRead, ReadOffset), kBytesToRead);

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

    ASSERT_EQ(emu_close(fd), 0);
    ASSERT_EQ(run_fsck(), 0);
    END_TEST;
}

constexpr size_t kBlockSize = 8192;
constexpr size_t kDirectBlocks = 16;

RUN_MINFS_TESTS(sparse_tests,
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
)
