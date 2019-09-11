// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <utility>

#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <fs-test-utils/fixture.h>
#include <minfs/format.h>
#include <minfs/fsck.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

namespace {

void RepairCorruptedSuperblock(fbl::unique_fd fd, const char* mount_path,
                               const char* ramdisk_path, uint32_t backup_location) {
  minfs::Superblock info;
  // Try reading the superblock.
  ASSERT_GE(pread(fd.get(), &info, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to read superblock.");
  ASSERT_EQ(minfs::kMinfsMagic0, info.magic0);

  // Try reading the backup superblock.
  minfs::Superblock backup_info;
  ASSERT_GE(pread(fd.get(), &backup_info, minfs::kMinfsBlockSize,
            backup_location * minfs::kMinfsBlockSize), 0, "Unable to read backup superblock.");
  ASSERT_EQ(minfs::kMinfsMagic0, backup_info.magic0);

  // Corrupt superblock, by erasing it completely from disk.
  memset(&info, 0, sizeof(info));
  ASSERT_GE(pwrite(fd.get(), &info, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to corrupt superblock.");

  // Running mount to repair the filesystem.
  ASSERT_EQ(ZX_OK, mount(fd.get(), mount_path, DISK_FORMAT_MINFS, &default_mount_options,
      launch_stdio_async));
  ASSERT_EQ(ZX_OK, umount(mount_path));

  // Mount consumes the fd, hence ramdisk needs to be opened again.
  int fd_mount = open(ramdisk_path, O_RDWR);
  ASSERT_GE(fd_mount, 0, "Could not open ramdisk device.");

  // Try reading the superblock.
  ASSERT_GE(pread(fd_mount, &info, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to read superblock.");

  // Confirm that the corrupted superblock is repaired by backup superblock.
  ASSERT_EQ(minfs::kMinfsMagic0, info.magic0);
}

// Tests backup superblock functionality on minfs backed with non-FVM block device.
TEST(NonFvmBackupSuperblockTest, NonFvmMountCorruptedSuperblock)  {
  const char* mount_path = "/tmp/mount_backup";
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
  const char* ramdisk_path = ramdisk_get_path(ramdisk);
  ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options), ZX_OK);
  ASSERT_EQ(mkdir(mount_path, 0666), 0);
  fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd);

  ASSERT_NO_FAILURES(RepairCorruptedSuperblock(std::move(fd), mount_path, ramdisk_path,
                     minfs::kNonFvmSuperblockBackup));

  ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
  ASSERT_EQ(unlink(mount_path), 0);
}

class FvmBackupSuperblockTest : public zxtest::Test {
 public:
  void SetUp() final {
    fs_test_utils::FixtureOptions options =
        fs_test_utils::FixtureOptions::Default(DISK_FORMAT_MINFS);
    options.isolated_devmgr = true;
    options.use_fvm = true;
    options.fs_mount = false;

    fixture_ = std::make_unique<fs_test_utils::Fixture>(options);
    ASSERT_EQ(ZX_OK, fixture_->SetUpTestCase());
    ASSERT_EQ(ZX_OK, fixture_->SetUp());
    ASSERT_EQ(mkdir(mount_path_, 0666), 0);
  }

  void TearDown() final {
    ASSERT_EQ(unlink(mount_path_), 0);
    ASSERT_EQ(ZX_OK, fixture_->TearDown());
    ASSERT_EQ(ZX_OK, fixture_->TearDownTestCase());
  }

  const char* partition_path() const { return fixture_->partition_path().c_str(); }
  const char* block_device_path() const { return fixture_->block_device_path().c_str(); }
  disk_format_t fs_type() const { return fixture_->options().fs_type; }
  const char* mount_path() const { return mount_path_; }

 private:
  const char* mount_path_ = "/tmp/mount_fvm_backup";
  std::unique_ptr<fs_test_utils::Fixture> fixture_;
};

// Tests backup superblock functionality on minfs backed with FVM block device.
TEST_F(FvmBackupSuperblockTest, FvmMountCorruptedSuperblock)  {
  fbl::unique_fd block_fd(open(block_device_path(), O_RDWR));
  ASSERT_TRUE(block_fd);
  fbl::unique_fd fs_fd(open(partition_path(), O_RDWR));
  ASSERT_TRUE(fs_fd);
  ASSERT_NO_FAILURES(RepairCorruptedSuperblock(std::move(fs_fd), mount_path(), partition_path(),
      minfs::kFvmSuperblockBackup));
}

// TODO(ZX-4623): Remove this code after migration to major version 8.
// Fills old superblock fields of version 7 from the newer version 8.
void FillOldSuperblockFields(minfs::Superblock *info, minfs::SuperblockOld *old_info) {
  old_info->magic0 = minfs::kMinfsMagic0;
  old_info->magic1 = minfs::kMinfsMagic1;
  old_info->version = minfs::kMinfsMajorVersionOld1;
  old_info->flags   = info->flags;
  old_info->block_size = info->block_size;
  old_info->inode_size = info->inode_size;
  old_info->block_count = info->block_count;
  old_info->inode_count = info->inode_count;
  old_info->alloc_block_count = info->alloc_block_count;
  old_info->alloc_inode_count = info->alloc_inode_count;
  old_info->ibm_block = info->ibm_block;
  old_info->abm_block = info->abm_block;
  old_info->ino_block = info->ino_block;
  old_info->journal_start_block = info->integrity_start_block + minfs::kBackupSuperblockBlocks;
  old_info->dat_block = info->dat_block;
  old_info->slice_size = info->slice_size;
  old_info->vslice_count = info->vslice_count;
  old_info->ibm_slices = info->ibm_slices;
  old_info->abm_slices = info->abm_slices;
  old_info->ino_slices = info->ino_slices;
  old_info->journal_slices = info->integrity_slices;
  old_info->dat_slices = info->dat_slices;
  old_info->unlinked_head = info->unlinked_head;
  old_info->unlinked_tail = info->unlinked_tail;
}

// TODO(ZX-4623): Remove this test after migration to major version 8.
// Tests upgrade from older superblock version 7 to version 9.
TEST_F(FvmBackupSuperblockTest, FvmUpgradeSuperblockV7) {
  fbl::unique_fd block_fd(open(block_device_path(), O_RDWR));
  ASSERT_TRUE(block_fd);
  fbl::unique_fd fs_fd(open(partition_path(), O_RDWR));
  ASSERT_TRUE(fs_fd);
  minfs::Superblock info;
  minfs::SuperblockOld old_info;

  // Try reading the superblock.
  ASSERT_GE(pread(fs_fd.get(), &info, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to read superblock.");

  FillOldSuperblockFields(&info, &old_info);

  // Erase the superblock from disk.
  memset(&info, 0, sizeof(info));
  ASSERT_GE(pwrite(fs_fd.get(), &info, minfs::kMinfsBlockSize,
            minfs::kSuperblockStart * minfs::kMinfsBlockSize), 0,
            "Unable to erase superblock.");

  // Erase the backup superblock from disk.
  ASSERT_GE(pwrite(fs_fd.get(), &info,
            minfs::kMinfsBlockSize, minfs::kFvmSuperblockBackup * minfs::kMinfsBlockSize), 0,
            "Unable to erase backup superblock.");

  // Write old version 7 superblock to disk.
  uint8_t block[minfs::kMinfsBlockSize];
  memset(block, 0, sizeof(info));
  memcpy(block, &old_info, sizeof(old_info));
  ASSERT_GE(pwrite(fs_fd.get(), block, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to write older superblock.");

  // Running mount to upgrade the filesystem.
  ASSERT_EQ(ZX_OK, mount(fs_fd.get(), mount_path(), DISK_FORMAT_MINFS, &default_mount_options,
      launch_stdio_async));
  ASSERT_EQ(ZX_OK, umount(mount_path()));

  // Mount consumes the fd, hence block device needs to be opened again.
  int fd_mount = open(partition_path(), O_RDWR);
  ASSERT_GE(fd_mount, 0, "Could not open block device.");

  // Try reading the superblock.
  ASSERT_GE(pread(fd_mount, &info, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to read superblock.");

  // Verify that the superblock was upgraded from version 7 to version 9.
  ASSERT_EQ(info.version_major, minfs::kMinfsMajorVersion);
  ASSERT_EQ(info.version_minor, minfs::kMinfsMinorVersion);
}

// TODO(36164): Remove this test after migration to major version 9.
// Tests upgrade from older superblock version 8 to version 9.
TEST_F(FvmBackupSuperblockTest, FvmUpgradeSuperblockV8) {
  fbl::unique_fd block_fd(open(block_device_path(), O_RDWR));
  ASSERT_TRUE(block_fd);
  fbl::unique_fd fs_fd(open(partition_path(), O_RDWR));
  ASSERT_TRUE(fs_fd);
  minfs::Superblock info;

  // Try reading the superblock.
  ASSERT_GE(pread(fs_fd.get(), &info, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to read superblock.");

  info.version_major = minfs::kMinfsMajorVersionOld2;

  // Write old version 8 superblock to disk.
  uint8_t block[minfs::kMinfsBlockSize];
  memset(block, 0, sizeof(info));
  memcpy(block, &info, sizeof(info));
  ASSERT_GE(pwrite(fs_fd.get(), block, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to write older superblock.");

  // Running mount to upgrade the filesystem.
  ASSERT_EQ(ZX_OK, mount(fs_fd.get(), mount_path(), DISK_FORMAT_MINFS, &default_mount_options,
      launch_stdio_async));
  ASSERT_EQ(ZX_OK, umount(mount_path()));

  // Mount consumes the fd, hence block device needs to be opened again.
  int fd_mount = open(partition_path(), O_RDWR);
  ASSERT_GE(fd_mount, 0, "Could not open block device.");

  // Try reading the superblock.
  ASSERT_GE(pread(fd_mount, &info, minfs::kMinfsBlockSize, minfs::kSuperblockStart), 0,
            "Unable to read superblock.");

  // Verify that the superblock was upgraded from version 8 to version 9.
  ASSERT_EQ(info.version_major, minfs::kMinfsMajorVersion);
  ASSERT_EQ(info.version_minor, minfs::kMinfsMinorVersion);
}

}  // namespace
