// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/minfs/format.h"

namespace minfs {
namespace {

using SuperblockTest = fs_test::FilesystemTest;

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

TEST_P(SuperblockTest, RepairCorruptSuperblock) {
  ASSERT_EQ(fs().Unmount().status_value(), ZX_OK);

  Superblock info;
  ReadSuperblock(fs().DevicePath().value(), kSuperblockStart, &info);
  ASSERT_EQ(kMinfsMagic0, info.magic0);

  // Corrupt superblock, by erasing it completely from disk.
  memset(&info, 0, sizeof(info));
  WriteSuperblock(fs().DevicePath().value(), info);

  // Running mount to repair the filesystem.
  ASSERT_EQ(fs().Mount().status_value(), ZX_OK);
  ASSERT_EQ(fs().Unmount().status_value(), ZX_OK);

  // Confirm that the corrupted superblock is repaired by backup superblock.
  ReadSuperblock(fs().DevicePath().value(), kSuperblockStart, &info);
  ASSERT_EQ(kMinfsMagic0, info.magic0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, SuperblockTest,
                         testing::ValuesIn(fs_test::AllTestFilesystems()),
                         testing::PrintToStringParamName());

enum class Comparison {
  kSame,
  kDifferent,
};

void CompareSuperblockAndBackupAllocCounts(const fs_test::TestFilesystem& fs, Comparison value) {
  Superblock info;
  ASSERT_NO_FATAL_FAILURE(ReadSuperblock(fs.DevicePath().value(), kSuperblockStart, &info));
  ASSERT_EQ(kMinfsMagic0, info.magic0);

  Superblock backup_info;
  ASSERT_NO_FATAL_FAILURE(
      ReadSuperblock(fs.DevicePath().value(), kNonFvmSuperblockBackup, &backup_info));
  ASSERT_EQ(kMinfsMagic0, backup_info.magic0);

  if (value == Comparison::kSame) {
    EXPECT_EQ(info.alloc_block_count, backup_info.alloc_block_count);
    EXPECT_EQ(info.alloc_inode_count, backup_info.alloc_inode_count);
  } else {
    EXPECT_NE(info.alloc_block_count, backup_info.alloc_block_count);
    EXPECT_NE(info.alloc_inode_count, backup_info.alloc_inode_count);
  }
}

void FsyncFilesystem(const std::string& mount_path) {
  // Open the root directory to fsync the filesystem.
  fbl::unique_fd fd_mount(open(mount_path.c_str(), O_RDONLY));
  ASSERT_TRUE(fd_mount);
  ASSERT_EQ(fsync(fd_mount.get()), 0);
}

class SuperblockTestNoFvm : public fs_test::BaseFilesystemTest {
 public:
  SuperblockTestNoFvm() : BaseFilesystemTest(fs_test::OptionsWithDescription("MinfsWithoutFvm")) {}
};

// Tests alloc_*_counts write frequency difference for backup superblock.
TEST_F(SuperblockTestNoFvm, AllocCountWriteFrequency) {
  ASSERT_NO_FATAL_FAILURE(CompareSuperblockAndBackupAllocCounts(fs(), Comparison::kSame));

  // Force allocating inodes as well as data blocks.
  ASSERT_EQ(mkdir(GetPath("test_dir").c_str(), 0755), 0);

  fbl::unique_fd fd_file(open(GetPath("test_dir/file").c_str(), O_CREAT | O_RDWR, 0666));
  ASSERT_TRUE(fd_file);

  // Write something to the file.
  char data[kMinfsBlockSize];
  memset(data, 0xb0b, sizeof(data));
  ASSERT_EQ(kMinfsBlockSize, write(fd_file.get(), data, kMinfsBlockSize));
  ASSERT_EQ(0, fsync(fd_file.get()));
  fd_file.reset();

  FsyncFilesystem(fs().mount_path());

  // Check that superblock and backup alloc counts are now different.
  ASSERT_NO_FATAL_FAILURE(CompareSuperblockAndBackupAllocCounts(fs(), Comparison::kDifferent));

  ASSERT_EQ(fs().Unmount().status_value(), ZX_OK);
  ASSERT_NO_FATAL_FAILURE(CompareSuperblockAndBackupAllocCounts(fs(), Comparison::kSame));
}

}  // namespace
}  // namespace minfs
