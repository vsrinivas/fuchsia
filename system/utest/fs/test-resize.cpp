// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <fbl/new.h>
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>

#include "filesystems.h"
#include "misc.h"

namespace {

bool test_use_all_inodes(void) {
    BEGIN_TEST;
    ASSERT_TRUE(test_info->supports_resize);

    // Create 100,000 inodes.
    // We expect that this will force enough inodes to cause the
    // filesystem structures to resize partway through.
    constexpr size_t kFilesPerDirectory = 100;
    constexpr size_t kDirectoryCount = 1000;
    for (size_t d = 0; d < kDirectoryCount; d++) {
        if (d % 100 == 0) {
            printf("Creating directory (containing 100 files): %lu\n", d);
        }
        char dname[128];
        snprintf(dname, sizeof(dname), "::%lu", d);
        ASSERT_EQ(mkdir(dname, 0666), 0);
        for (size_t f = 0; f < kFilesPerDirectory; f++) {
            char fname[128];
            snprintf(fname, sizeof(fname), "::%lu/%lu", d, f);
            int fd = open(fname, O_CREAT | O_RDWR | O_EXCL);
            ASSERT_GT(fd, 0);
            ASSERT_EQ(close(fd), 0);
        }
    }

    printf("Unmounting, Re-mounting, verifying...\n");
    ASSERT_TRUE(check_remount(), "Could not remount filesystem");

    for (size_t d = 0; d < kDirectoryCount; d++) {
        if (d % 100 == 0) {
            printf("Deleting directory (containing 100 files): %lu\n", d);
        }
        for (size_t f = 0; f < kFilesPerDirectory; f++) {
            char fname[128];
            snprintf(fname, sizeof(fname), "::%lu/%lu", d, f);
            ASSERT_EQ(unlink(fname), 0);
        }
        char dname[128];
        snprintf(dname, sizeof(dname), "::%lu", d);
        ASSERT_EQ(rmdir(dname), 0);
    }

    END_TEST;
}

bool test_use_all_data(void) {
    BEGIN_TEST;
    constexpr size_t kBufSize = (1 << 20);
    constexpr size_t kFileBufCount = 20;
    ASSERT_TRUE(test_info->supports_resize);

    uint64_t disk_size = test_disk_info.block_count * test_disk_info.block_size;
    size_t file_count = disk_size / kBufSize / kFileBufCount * 9 / 10;

    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[kBufSize]);
    ASSERT_TRUE(ac.check());
    memset(buf.get(), 0, kBufSize);

    for (size_t f = 0; f < file_count; f++) {
        printf("Creating 20 MB file #%lu\n", f);
        char fname[128];
        snprintf(fname, sizeof(fname), "::%lu", f);
        int fd = open(fname, O_CREAT | O_RDWR | O_EXCL);
        ASSERT_GT(fd, 0);
        for (size_t i = 0; i < kFileBufCount; i++) {
            ASSERT_EQ(write(fd, buf.get(), kBufSize), kBufSize);
        }
        ASSERT_EQ(close(fd), 0);
    }

    ASSERT_TRUE(check_remount(), "Could not remount filesystem");

    for (size_t f = 0; f < file_count; f++) {
        char fname[128];
        snprintf(fname, sizeof(fname), "::%lu", f);
        ASSERT_EQ(unlink(fname), 0);
    }

    END_TEST;
}

const test_disk_t disk = {
    .block_count = 1LLU << 17,
    .block_size = 1LLU << 9,
    .slice_size = 1LLU << 22,
};

}  // namespace

// Reformat the disk between tests to restore original size.
RUN_FOR_ALL_FILESYSTEMS_TYPE(fs_resize_tests_inodes, disk, FS_TEST_FVM,
    RUN_TEST_LARGE(test_use_all_inodes)
)

RUN_FOR_ALL_FILESYSTEMS_TYPE(fs_resize_tests_data, disk, FS_TEST_FVM,
    RUN_TEST_LARGE(test_use_all_data)
)
