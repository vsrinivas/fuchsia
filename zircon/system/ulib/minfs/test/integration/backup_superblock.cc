// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "minfs_fixtures.h"
#include "utils.h"

namespace {

using fs::FilesystemTest;
using minfs::kMinfsBlockSize;
using minfs::kMinfsMagic0;
using minfs::kSuperblockStart;
using minfs::Superblock;

using SuperblockTest = MinfsTest;
using SuperblockTestWithFvm = MinfsTestWithFvm;

void ReadSuperblock(const std::string& device_path, uint64_t block_num, Superblock* info) {
  fbl::unique_fd device(open(device_path.c_str(), O_RDWR));
  ASSERT_TRUE(device);
  ASSERT_EQ(kMinfsBlockSize,
            pread(device.get(), info, kMinfsBlockSize, block_num * kMinfsBlockSize));
}

void WriteSuperblock(const std::string& device_path, const Superblock& info) {
  fbl::unique_fd device(open(device_path.c_str(), O_RDWR));
  ASSERT_TRUE(device);
  ASSERT_EQ(kMinfsBlockSize,
            pwrite(device.get(), &info, kMinfsBlockSize, kSuperblockStart * kMinfsBlockSize));
}

// Tests backup superblock functionality.
void RunRepairCorruptSuperblockTest(FilesystemTest* test) {
  ASSERT_NO_FAILURES(test->Unmount());

  Superblock info;
  ReadSuperblock(test->device_path(), kSuperblockStart, &info);
  ASSERT_EQ(kMinfsMagic0, info.magic0);

  // Corrupt superblock, by erasing it completely from disk.
  memset(&info, 0, sizeof(info));
  WriteSuperblock(test->device_path(), info);

  // Running mount to repair the filesystem.
  ASSERT_NO_FAILURES(test->Mount());
  ASSERT_NO_FAILURES(test->Unmount());

  // Confirm that the corrupted superblock is repaired by backup superblock.
  ReadSuperblock(test->device_path(), kSuperblockStart, &info);
  ASSERT_EQ(kMinfsMagic0, info.magic0);
}

TEST_F(SuperblockTest, RepairCorruptSuperblock) { RunRepairCorruptSuperblockTest(this); }

TEST_F(SuperblockTestWithFvm, RepairCorruptSuperblock) { RunRepairCorruptSuperblockTest(this); }

enum class Comparison {
  kSame,
  kDifferent,
};

void CompareSuperblockAndBackupAllocCounts(const std::string& device_path, Comparison value) {
  Superblock info;
  ReadSuperblock(device_path, kSuperblockStart, &info);
  ASSERT_EQ(kMinfsMagic0, info.magic0);

  Superblock backup_info;
  ReadSuperblock(device_path, minfs::kNonFvmSuperblockBackup, &backup_info);
  ASSERT_EQ(kMinfsMagic0, backup_info.magic0);

  if (value == Comparison::kSame) {
    EXPECT_EQ(info.alloc_block_count, backup_info.alloc_block_count);
    EXPECT_EQ(info.alloc_inode_count, backup_info.alloc_inode_count);
  } else {
    EXPECT_NE(info.alloc_block_count, backup_info.alloc_block_count);
    EXPECT_NE(info.alloc_inode_count, backup_info.alloc_inode_count);
  }
}

void FsyncFilesystem() {
  // Open the root directory to fsync the filesystem.
  fbl::unique_fd fd_mount(open(kMountPath, O_RDONLY));
  ASSERT_TRUE(fd_mount);
  ASSERT_EQ(fsync(fd_mount.get()), 0);
}

// Tests alloc_*_counts write frequency difference for backup superblock.
TEST_F(SuperblockTest, AllocCountWriteFrequency) {
  ASSERT_NO_FAILURES(CompareSuperblockAndBackupAllocCounts(device_path(), Comparison::kSame));

  // Force allocating inodes as well as data blocks.
  ASSERT_TRUE(CreateDirectory("/test_dir"));

  fbl::unique_fd fd_file = CreateFile("/test_dir/file");
  ASSERT_TRUE(fd_file);

  // Write something to the file.
  char data[kMinfsBlockSize];
  memset(data, 0xb0b, sizeof(data));
  ASSERT_EQ(kMinfsBlockSize, write(fd_file.get(), data, kMinfsBlockSize));
  ASSERT_EQ(0, fsync(fd_file.get()));
  fd_file.reset();

  FsyncFilesystem();

  // Check that superblock and backup alloc counts are now different.
  ASSERT_NO_FAILURES(CompareSuperblockAndBackupAllocCounts(device_path(), Comparison::kDifferent));

  ASSERT_NO_FAILURES(Unmount());
  ASSERT_NO_FAILURES(CompareSuperblockAndBackupAllocCounts(device_path(), Comparison::kSame));
}

}  // namespace
