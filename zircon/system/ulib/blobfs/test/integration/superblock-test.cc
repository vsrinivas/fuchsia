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
#include <blobfs/format.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

namespace {

void FsyncFilesystem(const char* mount_path) {
  // Open the root directory to fsync the filesystem.
  fbl::unique_fd fd_mount(open(mount_path, O_RDONLY));
  ASSERT_TRUE(fd_mount);
  ASSERT_EQ(fsync(fd_mount.get()), 0);
  ASSERT_EQ(close(fd_mount.release()), 0);
}

void CheckDirtyBitOnMount(fbl::unique_fd fd, const char* mount_path,
                          const char* ramdisk_path) {
  blobfs::Superblock info;

  // Running mount to set dirty bit.
  ASSERT_EQ(ZX_OK, mount(fd.release(), mount_path, DISK_FORMAT_BLOBFS, &default_mount_options,
            launch_stdio_async));

  // Mount consumes the fd, hence ramdisk needs to be opened again.
  fbl::unique_fd fd_device(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd_device);

  ASSERT_NO_FAILURES(FsyncFilesystem(mount_path));

  // Try reading the superblock.
  ASSERT_GE(pread(fd_device.get(), &info, blobfs::kBlobfsBlockSize, 0), 0,
            "Unable to read superblock.");

  // Check if clean bit is unset.
  ASSERT_EQ(0, info.flags & blobfs::kBlobFlagClean);

  // Unmount and check if clean bit is set.
  ASSERT_EQ(ZX_OK, umount(mount_path));

  // Try reading the superblock.
  ASSERT_GE(pread(fd_device.get(), &info, blobfs::kBlobfsBlockSize, 0), 0,
            "Unable to read superblock.");

  // Check clean flag set properly on unmount.
  ASSERT_EQ(blobfs::kBlobFlagClean, info.flags & blobfs::kBlobFlagClean);
}

// Tests dirty bit flag in superblock functionality on blobfs backed with non-FVM block device.
TEST(NonFvmSuperblockTest, NonFvmCheckDirtyBitOnMount)  {
  const char* mount_path = "/tmp/mount_blobfs";
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
  const char* ramdisk_path = ramdisk_get_path(ramdisk);
  ASSERT_EQ(mkfs(ramdisk_path, DISK_FORMAT_BLOBFS, launch_stdio_sync, &default_mkfs_options),
            ZX_OK);
  ASSERT_EQ(mkdir(mount_path, 0666), 0);
  fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd);

  ASSERT_NO_FAILURES(CheckDirtyBitOnMount(std::move(fd), mount_path, ramdisk_path));

  ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
  ASSERT_EQ(unlink(mount_path), 0);
}

class FvmSuperblockTest : public zxtest::Test {
 public:
  void SetUp() final {
    fs_test_utils::FixtureOptions options =
        fs_test_utils::FixtureOptions::Default(DISK_FORMAT_BLOBFS);
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
  const char* mount_path_ = "/tmp/mount_fvm_blobfs";
  std::unique_ptr<fs_test_utils::Fixture> fixture_;
};

// Tests dirty bit flag in superblock functionality on blobfs backed with FVM block device.
TEST_F(FvmSuperblockTest, FvmCheckDirtyBitOnMount)  {
  fbl::unique_fd block_fd(open(block_device_path(), O_RDWR));
  ASSERT_TRUE(block_fd);
  fbl::unique_fd fs_fd(open(partition_path(), O_RDWR));
  ASSERT_TRUE(fs_fd);
  ASSERT_NO_FAILURES(CheckDirtyBitOnMount(std::move(fs_fd), mount_path(), partition_path()));
}

}  // namespace
