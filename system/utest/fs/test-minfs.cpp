// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for MinFS-specific behavior.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <lib/fdio/vfs.h>
#include <minfs/format.h>
#include <unittest/unittest.h>
#include <zircon/device/vfs.h>

#include "filesystems.h"
#include "misc.h"

namespace {

bool QueryInfo(char* buf, size_t buf_size) {
    BEGIN_HELPER;
    ASSERT_GE(buf_size, sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1);

    int fd = open(kMountPath, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);

    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t rv = ioctl_vfs_query_fs(fd, info, buf_size - 1);
    ASSERT_EQ(close(fd), 0);

    ASSERT_EQ(rv, sizeof(vfs_query_info_t) + strlen("minfs"), "Failed to query filesystem");

    buf[rv] = '\0';  // NULL terminate the name.
    ASSERT_EQ(strncmp("minfs", info->name, strlen("minfs")), 0);
    ASSERT_EQ(info->block_size, minfs::kMinfsBlockSize);
    ASSERT_EQ(info->max_filename_size, minfs::kMinfsMaxNameSize);
    ASSERT_EQ(info->fs_type, VFS_TYPE_MINFS);
    ASSERT_NE(info->fs_id, 0);

    ASSERT_EQ(info->used_bytes % info->block_size, 0);
    ASSERT_EQ(info->total_bytes % info->block_size, 0);
    END_HELPER;
}

bool VerifyQueryInfo(size_t expected_nodes) {
    BEGIN_HELPER;
    size_t buf_size = sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1;
    char buf[buf_size];

    ASSERT_TRUE(QueryInfo(&buf[0], buf_size));

    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);
    ASSERT_EQ(info->total_bytes, 8 * 1024 * 1024);

    // TODO(ZX-1372): Adjust this once minfs accounting on truncate is fixed.
    ASSERT_EQ(info->used_bytes, 2 * minfs::kMinfsBlockSize);
    ASSERT_EQ(info->total_nodes, 32 * 1024);
    ASSERT_EQ(info->used_nodes, expected_nodes + 2);
    END_HELPER;
}

bool GetUsedBlocks(uint32_t* used_blocks) {
    BEGIN_HELPER;
    size_t buf_size = sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1;
    char buf[buf_size];
    ASSERT_TRUE(QueryInfo(&buf[0], buf_size));
    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);

    *used_blocks = static_cast<uint32_t>((info->total_bytes - info->used_bytes) / info->block_size);
    END_HELPER;
}

}  // namespace

bool TestQueryInfo(void) {
    BEGIN_TEST;
    ASSERT_TRUE(VerifyQueryInfo(0));
    for (int i = 0; i < 16; i++) {
        char path[128];
        snprintf(path, sizeof(path) - 1, "%s/file_%d", kMountPath, i);

        int fd = open(path, O_CREAT | O_RDWR);
        ASSERT_GT(fd, 0, "Failed to create file");
        ASSERT_EQ(ftruncate(fd, 30 * 1024), 0);
        ASSERT_EQ(close(fd), 0);
    }

    ASSERT_TRUE(VerifyQueryInfo(16));
    END_TEST;
}

// Write to the file until at most |max_remaining_blocks| remain in the partition.
// Return the new remaining block count as |actual_remaining_blocks|.
bool FillPartition(int fd, uint32_t max_remaining_blocks, uint32_t* actual_remaining_blocks) {
    BEGIN_HELPER;
    char data[minfs::kMinfsBlockSize];
    memset(data, 0xaa, sizeof(data));
    uint32_t free_blocks;

    while (true) {
        ASSERT_TRUE(GetUsedBlocks(&free_blocks));
        if (free_blocks <= max_remaining_blocks) {
            break;
        }

        ASSERT_EQ(write(fd, data, sizeof(data)), sizeof(data));
    }

    ASSERT_LE(free_blocks, max_remaining_blocks);
    ASSERT_GT(free_blocks, 0);

    *actual_remaining_blocks = free_blocks;
    END_HELPER;
}

// Return number of blocks allocated by the file at |fd|.
bool GetFileBlocks(int fd, uint64_t* blocks) {
    BEGIN_HELPER;
    struct stat stats;
    ASSERT_EQ(fstat(fd, &stats), 0);
    off_t size = stats.st_blocks * VNATTR_BLKSIZE;
    ASSERT_EQ(size % minfs::kMinfsBlockSize, 0);
    *blocks = static_cast<uint64_t>(size / minfs::kMinfsBlockSize);
    END_HELPER;
}

// Fill a directory to at most |max_blocks| full of direntries.
// We assume the directory is empty to begin with, and any files we are adding do not already exist.
bool FillDirectory(int dir_fd, uint32_t max_blocks) {
    BEGIN_HELPER;

    uint32_t file_count = 0;
    while (true) {
        char path[128];
        snprintf(path, sizeof(path) - 1, "file_%u", file_count++);
        fbl::unique_fd fd(openat(dir_fd, path, O_CREAT | O_RDWR));
        ASSERT_TRUE(fd);

        uint64_t current_blocks;
        ASSERT_TRUE(GetFileBlocks(dir_fd, &current_blocks));

        if (current_blocks > max_blocks) {
            ASSERT_EQ(unlinkat(dir_fd, path, 0), 0);
            break;
        }
    }

    END_HELPER;
}

// Test various operations when the Minfs partition is near capacity.
bool TestFullOperations(void) {
    BEGIN_TEST;

    // Define file names we will use upfront.
    const char* big_path = "big_file";
    const char* med_path = "med_file";
    const char* sml_path = "sml_file";

    // Open the mount point and create three files.
    fbl::unique_fd mnt_fd(open(kMountPath, O_RDONLY));
    ASSERT_TRUE(mnt_fd);

    fbl::unique_fd big_fd(openat(mnt_fd.get(), big_path, O_CREAT | O_RDWR));
    ASSERT_TRUE(big_fd);

    fbl::unique_fd med_fd(openat(mnt_fd.get(), med_path, O_CREAT | O_RDWR));
    ASSERT_TRUE(med_fd);

    fbl::unique_fd sml_fd(openat(mnt_fd.get(), sml_path, O_CREAT | O_RDWR));
    ASSERT_TRUE(sml_fd);

    // Write to the "big" file, filling the partition
    // and leaving at most kMinfsDirect + 1 blocks unused.
    uint32_t free_blocks = minfs::kMinfsDirect + 1;
    uint32_t actual_blocks;
    ASSERT_TRUE(FillPartition(big_fd.get(), free_blocks, &actual_blocks));

    // Write enough data to the second file to take up all remaining blocks except for 1.
    // This should strictly be writing to the direct block section of the file.
    char data[minfs::kMinfsBlockSize];
    memset(data, 0xaa, sizeof(data));
    for (unsigned i = 0; i < actual_blocks - 1; i++) {
        ASSERT_EQ(write(med_fd.get(), data, sizeof(data)), sizeof(data));
    }

    // Make sure we now have only 1 block remaining.
    ASSERT_TRUE(GetUsedBlocks(&free_blocks));
    ASSERT_EQ(free_blocks, 1);

    // We should now have exactly 1 free block remaining. Attempt to write into the indirect
    // section of the file so we ensure that at least 2 blocks are required.
    // This is expected to fail.
    ASSERT_EQ(lseek(med_fd.get(), minfs::kMinfsBlockSize * minfs::kMinfsDirect, SEEK_SET),
              minfs::kMinfsBlockSize * minfs::kMinfsDirect);
    ASSERT_LT(write(med_fd.get(), data, sizeof(data)), 0);

    // Since the last operation failed, we should still have 1 free block remaining. Writing to the
    // beginning of the second file should only require 1 (direct) block, and therefore pass.
    // Note: This fails without block reservation.
    ASSERT_EQ(write(sml_fd.get(), data, sizeof(data)), sizeof(data));

    // Without block reservation, something from the failed write remains allocated. Try editing
    // nearby blocks to force a writeback of partially allocated data.
    // Note: This will likely fail without block reservation.
    struct stat s;
    ASSERT_EQ(fstat(big_fd.get(), &s), 0);
    ssize_t truncate_size = fbl::round_up(static_cast<uint64_t>(s.st_size / 2),
                                          minfs::kMinfsBlockSize);
    ASSERT_EQ(ftruncate(big_fd.get(), truncate_size), 0);
    ASSERT_TRUE(check_remount());

    // Re-open files.
    mnt_fd.reset(open(kMountPath, O_RDONLY));
    ASSERT_TRUE(mnt_fd);
    big_fd.reset(openat(mnt_fd.get(), big_path, O_RDWR));
    ASSERT_TRUE(big_fd);
    sml_fd.reset(openat(mnt_fd.get(), sml_path, O_RDWR));
    ASSERT_TRUE(sml_fd);

    // Make sure we now have at least kMinfsDirect + 1 blocks remaining.
    ASSERT_TRUE(GetUsedBlocks(&free_blocks));
    ASSERT_GE(free_blocks, minfs::kMinfsDirect + 1);

    // We have some room now, so create a new directory.
    const char* dir_path = "directory";
    ASSERT_EQ(mkdirat(mnt_fd.get(), dir_path, 0666), 0);
    fbl::unique_fd dir_fd(openat(mnt_fd.get(), dir_path, O_RDONLY));
    ASSERT_TRUE(dir_fd);

    // Fill the directory up to kMinfsDirect blocks full of direntries.
    ASSERT_TRUE(FillDirectory(dir_fd.get(), minfs::kMinfsDirect));

    // Now re-fill the partition by writing as much as possible back to the original file.
    // Attempt to leave 1 block free.
    ASSERT_EQ(lseek(big_fd.get(), truncate_size, SEEK_SET), truncate_size);
    free_blocks = 1;
    ASSERT_TRUE(FillPartition(big_fd.get(), free_blocks, &actual_blocks));

    if (actual_blocks == 0) {
        // It is possible that, in our previous allocation of big_fd, we ended up leaving less than
        // |free_blocks| free. Since the file has grown potentially large, it is possible that
        // allocating a single block will also allocate additional indirect blocks.
        // For example, in a case where we have 2 free blocks remaining and expect to allocate 1,
        // we may actually end up allocating 2 instead, leaving us with 0 free blocks.
        // Since sml_fd is using less than kMinfsDirect blocks and thus is guaranteed to have a 1:1
        // block usage ratio, we can remedy this situation by removing a single block from sml_fd.
        ASSERT_EQ(ftruncate(sml_fd.get(), 0), 0);
    }

    while (actual_blocks > free_blocks) {
        // Otherwise, if too many blocks remain (if e.g. we needed to allocate 3 blocks but only 2
        // are remaining), write to sml_fd until only 1 remains.
        ASSERT_EQ(write(sml_fd.get(), data, sizeof(data)), sizeof(data));
        actual_blocks--;
    }

    // Ensure that there is now exactly one block remaining.
    ASSERT_TRUE(GetUsedBlocks(&actual_blocks));
    ASSERT_EQ(free_blocks, actual_blocks);

    // Now, attempt to add one more file to the directory we created. Since it will need to
    // allocate 2 blocks (1 indirect + 1 direct) and there is only 1 remaining, it should fail.
    uint64_t block_count;
    ASSERT_TRUE(GetFileBlocks(dir_fd.get(), &block_count));
    ASSERT_EQ(block_count, minfs::kMinfsDirect);
    fbl::unique_fd tmp_fd(openat(dir_fd.get(), "new_file", O_CREAT | O_RDWR));
    ASSERT_FALSE(tmp_fd);

    // Again, try editing nearby blocks to force bad allocation leftovers to be persisted, and
    // remount the partition. This is expected to fail without block reservation.
    ASSERT_EQ(fstat(big_fd.get(), &s), 0);
    ASSERT_EQ(s.st_size % minfs::kMinfsBlockSize, 0);
    truncate_size = s.st_size - minfs::kMinfsBlockSize;
    ASSERT_EQ(ftruncate(big_fd.get(), truncate_size), 0);
    ASSERT_TRUE(check_remount());

    // Re-open files.
    mnt_fd.reset(open(kMountPath, O_RDONLY));
    ASSERT_TRUE(mnt_fd);
    big_fd.reset(openat(mnt_fd.get(), big_path, O_RDWR));
    ASSERT_TRUE(big_fd);
    sml_fd.reset(openat(mnt_fd.get(), sml_path, O_RDWR));
    ASSERT_TRUE(sml_fd);

    // Fill the partition again, writing one block of data to sml_fd
    // in case we need an emergency truncate.
    ASSERT_EQ(write(sml_fd.get(), data, sizeof(data)), sizeof(data));
    ASSERT_EQ(lseek(big_fd.get(), truncate_size, SEEK_SET), truncate_size);
    free_blocks = 1;
    ASSERT_TRUE(FillPartition(big_fd.get(), free_blocks, &actual_blocks));

    if (actual_blocks == 0) {
        // If we ended up with fewer blocks than expected, truncate sml_fd to create more space.
        // (See note above for details.)
        ASSERT_EQ(ftruncate(sml_fd.get(), 0), 0);
    }

    while (actual_blocks > free_blocks) {
        // Otherwise, if too many blocks remain (if e.g. we needed to allocate 3 blocks but only 2
        // are remaining), write to sml_fd until only 1 remains.
        ASSERT_EQ(write(sml_fd.get(), data, sizeof(data)), sizeof(data));
        actual_blocks--;
    }

    // Ensure that there is now exactly one block remaining.
    ASSERT_TRUE(GetUsedBlocks(&actual_blocks));
    ASSERT_EQ(free_blocks, actual_blocks);

    // Now, attempt to rename one of our original files under the new directory.
    // This should also fail.
    ASSERT_NE(renameat(mnt_fd.get(), med_path, dir_fd.get(), med_path), 0);

    // Again, truncate the original file and attempt to remount.
    // Again, this should fail without block reservation.
    ASSERT_EQ(fstat(big_fd.get(), &s), 0);
    ASSERT_EQ(s.st_size % minfs::kMinfsBlockSize, 0);
    truncate_size = s.st_size - minfs::kMinfsBlockSize;
    ASSERT_EQ(ftruncate(big_fd.get(), truncate_size), 0);
    ASSERT_TRUE(check_remount());
    END_TEST;
}

#define RUN_MINFS_TESTS_NORMAL(name, CASE_TESTS) \
    FS_TEST_CASE(name, default_test_disk, CASE_TESTS, FS_TEST_NORMAL, minfs, 1)

#define RUN_MINFS_TESTS_FVM(name, CASE_TESTS) \
    FS_TEST_CASE(name##_fvm, default_test_disk, CASE_TESTS, FS_TEST_FVM, minfs, 1)

RUN_MINFS_TESTS_NORMAL(FsMinfsTests,
    RUN_TEST_LARGE(TestFullOperations)
)

RUN_MINFS_TESTS_FVM(FsMinfsFvmTests,
    RUN_TEST_MEDIUM(TestQueryInfo)
)
