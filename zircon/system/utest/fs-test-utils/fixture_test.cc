// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include <fcntl.h>
#include <stdio.h>

#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fs-test-utils/fixture.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <ramdevice-client/ramdisk.h>
#include <unittest/unittest.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>

namespace fs_test_utils {
namespace {

zx_status_t GetBlockDeviceInfo(const fbl::String& block_device_path,
                               fuchsia_hardware_block_BlockInfo* blk_info) {
  fbl::unique_fd fd(open(block_device_path.c_str(), O_RDONLY));
  fdio_cpp::FdioCaller disk_caller(std::move(fd));
  zx_status_t status;
  zx_status_t io_status =
      fuchsia_hardware_block_BlockGetInfo(disk_caller.borrow_channel(), &status, blk_info);
  if (io_status != ZX_OK) {
    return io_status;
  }
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

bool IsValidBlockDeviceOnlyTrue() {
  BEGIN_TEST;
  FixtureOptions options;
  fbl::String err_str;
  options.block_device_path = "some_block_device";
  ASSERT_TRUE(options.IsValid(&err_str), err_str.c_str());
  ASSERT_TRUE(err_str.empty());
  END_TEST;
}

bool IsValidUseRamdiskTrue() {
  BEGIN_TEST;
  FixtureOptions options;
  fbl::String err_str;
  options.use_ramdisk = true;
  options.ramdisk_block_size = 512;
  options.ramdisk_block_count = 1;
  ASSERT_TRUE(options.IsValid(&err_str), err_str.c_str());
  ASSERT_TRUE(err_str.empty());
  END_TEST;
}

bool IsValidUseFvmTrue() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
  fbl::String err_str;
  options.use_fvm = true;
  options.fvm_slice_size = kFvmBlockSize;
  ASSERT_TRUE(options.IsValid(&err_str), err_str.c_str());
  ASSERT_TRUE(err_str.empty());
  END_TEST;
}

bool IsValidEmptyIsFalse() {
  BEGIN_TEST;
  FixtureOptions options;
  fbl::String err_str;
  ASSERT_FALSE(options.IsValid(&err_str));
  ASSERT_FALSE(err_str.empty());
  END_TEST;
}

bool IsValidDefaultIsTrue() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
  fbl::String err_str;
  ASSERT_TRUE(options.IsValid(&err_str));
  ASSERT_TRUE(err_str.empty());
  END_TEST;
}

bool IsValidBlockAndRamdiskSetIsFalse() {
  BEGIN_TEST;
  FixtureOptions options;
  fbl::String err_str;
  options.block_device_path = "some_block_device";
  options.use_ramdisk = true;
  ASSERT_FALSE(options.IsValid(&err_str));
  ASSERT_FALSE(err_str.empty());
  END_TEST;
}

bool IsValidRamdiskBlockCountIsZeroFalse() {
  BEGIN_TEST;
  FixtureOptions options;
  fbl::String err_str;
  options.use_ramdisk = true;
  options.ramdisk_block_count = 0;
  options.ramdisk_block_size = 512;
  ASSERT_FALSE(options.IsValid(&err_str));
  ASSERT_FALSE(err_str.empty());
  END_TEST;
}

bool IsValidRamdiskBlockSizeIsZeroFalse() {
  BEGIN_TEST;
  FixtureOptions options;
  fbl::String err_str;
  options.use_ramdisk = true;
  options.ramdisk_block_count = 10;
  options.ramdisk_block_size = 0;
  ASSERT_FALSE(options.IsValid(&err_str));
  ASSERT_FALSE(err_str.empty());
  END_TEST;
}

bool IsValidFvmSlizeSizeIsZeroFalse() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
  fbl::String err_str;
  options.use_fvm = true;
  options.fvm_slice_size = 0;
  ASSERT_FALSE(options.IsValid(&err_str));
  ASSERT_FALSE(err_str.empty());
  END_TEST;
}

bool IsValidFvmSlizeSizeIsNotMultipleOfFvmBlockSizeFalse() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
  fbl::String err_str;
  options.use_fvm = true;
  options.fvm_slice_size = kFvmBlockSize + 3;
  ASSERT_FALSE(options.IsValid(&err_str));
  ASSERT_FALSE(err_str.empty());
  END_TEST;
}

bool IsValidNoBlockDeviceFalse() {
  BEGIN_TEST;
  FixtureOptions options;
  fbl::String err_str;
  options.block_device_path = "";
  ASSERT_FALSE(options.IsValid(&err_str));
  ASSERT_FALSE(err_str.empty());
  END_TEST;
}

BEGIN_TEST_CASE(FixtureOptionsTests);
RUN_TEST(IsValidBlockDeviceOnlyTrue);
RUN_TEST(IsValidUseRamdiskTrue);
RUN_TEST(IsValidUseFvmTrue);
RUN_TEST(IsValidDefaultIsTrue);
RUN_TEST(IsValidEmptyIsFalse);
RUN_TEST(IsValidNoBlockDeviceFalse);
RUN_TEST(IsValidBlockAndRamdiskSetIsFalse);
RUN_TEST(IsValidRamdiskBlockCountIsZeroFalse);
RUN_TEST(IsValidRamdiskBlockSizeIsZeroFalse);
RUN_TEST(IsValidFvmSlizeSizeIsZeroFalse);
RUN_TEST(IsValidFvmSlizeSizeIsNotMultipleOfFvmBlockSizeFalse);
END_TEST_CASE(FixtureOptionsTests)

bool RamdiskSetupAndCleanup() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
  options.isolated_devmgr = true;
  Fixture fixture(options);
  fixture.SetUpTestCase();
  ASSERT_TRUE(!fixture.block_device_path().empty());
  fuchsia_hardware_block_BlockInfo ramdisk_info;
  ASSERT_EQ(GetBlockDeviceInfo(fixture.block_device_path(), &ramdisk_info), ZX_OK);
  ASSERT_EQ(ramdisk_info.block_count, options.ramdisk_block_count);
  ASSERT_EQ(ramdisk_info.block_size, options.ramdisk_block_size);
  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);
  fbl::unique_fd ramdisk_fd(open(fixture.block_device_path().c_str(), O_RDONLY));
  ASSERT_FALSE(ramdisk_fd);
  END_TEST;
}

bool DiskIsFormattedCorrectlyNoFvm() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_MINFS);
  options.isolated_devmgr = true;
  Fixture fixture(options);
  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  // Check device format.
  fbl::unique_fd blk_fd(open(fixture.GetFsBlockDevice().c_str(), O_RDONLY));
  ASSERT_TRUE(blk_fd);
  disk_format_t actual = detect_disk_format(blk_fd.get());
  ASSERT_EQ(actual, DISK_FORMAT_MINFS);
  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  // Verify nothing is mounted anymore.
  ASSERT_EQ(umount(fixture.fs_path().c_str()), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);
  END_TEST;
}

bool DiskAndFvmAreFormattedCorrectly() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_MINFS);
  options.isolated_devmgr = true;
  options.use_fvm = true;
  Fixture fixture(options);
  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  // Check device format.
  fbl::unique_fd blk_fd(open(fixture.GetFsBlockDevice().c_str(), O_RDONLY));
  ASSERT_TRUE(blk_fd);
  disk_format_t actual = detect_disk_format(blk_fd.get());
  ASSERT_EQ(actual, DISK_FORMAT_MINFS);

  fbl::unique_fd fvm_blk_fd(open(fixture.block_device_path().c_str(), O_RDONLY));
  ASSERT_TRUE(fvm_blk_fd);
  disk_format_t fvm_actual = detect_disk_format(fvm_blk_fd.get());
  ASSERT_EQ(fvm_actual, DISK_FORMAT_FVM);

  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  // Verify nothing is mounted anymore.
  ASSERT_EQ(umount(fixture.fs_path().c_str()), ZX_ERR_NOT_FOUND);

  ASSERT_TRUE(fvm_blk_fd);
  fvm_actual = detect_disk_format(fvm_blk_fd.get());
  ASSERT_EQ(fvm_actual, DISK_FORMAT_UNKNOWN);

  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);
  END_TEST;
}

bool UseBlockDeviceIsOk() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_MINFS);
  options.use_ramdisk = false;
  options.isolated_devmgr = false;

  // Create a Ramdisk which will be passed as the 'block_device'.
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(options.ramdisk_block_size, options.ramdisk_block_count, &ramdisk),
            ZX_OK);
  options.block_device_path = ramdisk_get_path(ramdisk);

  auto clean_up = fbl::MakeAutoCall([&ramdisk]() { ramdisk_destroy(ramdisk); });

  mkfs_options_t mkfs_options = default_mkfs_options;
  ASSERT_EQ(
      mkfs(options.block_device_path.c_str(), DISK_FORMAT_BLOBFS, launch_stdio_sync, &mkfs_options),
      ZX_OK);

  Fixture fixture(options);

  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  EXPECT_TRUE(options.block_device_path == fixture.block_device_path());
  EXPECT_TRUE(options.block_device_path == fixture.GetFsBlockDevice());
  fbl::unique_fd blk_fd(open(fixture.block_device_path().c_str(), O_RDONLY));
  ASSERT_TRUE(blk_fd);
  disk_format_t actual_format = detect_disk_format(blk_fd.get());
  ASSERT_EQ(actual_format, DISK_FORMAT_BLOBFS);

  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  blk_fd.reset(open(fixture.block_device_path().c_str(), O_RDONLY));
  ASSERT_TRUE(blk_fd);
  actual_format = detect_disk_format(blk_fd.get());
  ASSERT_EQ(actual_format, DISK_FORMAT_MINFS);

  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  blk_fd.reset(open(fixture.block_device_path().c_str(), O_RDONLY));
  ASSERT_TRUE(blk_fd);
  actual_format = detect_disk_format(blk_fd.get());
  ASSERT_EQ(actual_format, DISK_FORMAT_MINFS);

  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);
  END_TEST;
}

bool UseBlockDeviceWithFvmIsOk() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_MINFS);
  options.use_ramdisk = false;
  options.use_fvm = true;
  options.isolated_devmgr = false;

  // Create a Ramdisk which will be passed as the 'block_device'.
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(options.ramdisk_block_size, options.ramdisk_block_count, &ramdisk),
            ZX_OK);
  options.block_device_path = ramdisk_get_path(ramdisk);

  auto clean_up = fbl::MakeAutoCall([&ramdisk]() { ramdisk_destroy(ramdisk); });

  mkfs_options_t mkfs_options = default_mkfs_options;
  ASSERT_EQ(
      mkfs(options.block_device_path.c_str(), DISK_FORMAT_BLOBFS, launch_stdio_sync, &mkfs_options),
      ZX_OK);

  Fixture fixture(options);

  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  EXPECT_TRUE(options.block_device_path == fixture.block_device_path());
  EXPECT_TRUE(fixture.GetFsBlockDevice().empty());
  fbl::unique_fd blk_fd(open(fixture.block_device_path().c_str(), O_RDONLY));
  ASSERT_TRUE(blk_fd);
  disk_format_t actual_format = detect_disk_format(blk_fd.get());
  ASSERT_EQ(actual_format, DISK_FORMAT_BLOBFS);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  blk_fd.reset(open(fixture.block_device_path().c_str(), O_RDONLY));
  ASSERT_TRUE(blk_fd);
  actual_format = detect_disk_format(blk_fd.get());
  ASSERT_EQ(actual_format, DISK_FORMAT_FVM);

  blk_fd.reset(open(fixture.GetFsBlockDevice().c_str(), O_RDONLY));
  ASSERT_TRUE(blk_fd);
  actual_format = detect_disk_format(blk_fd.get());
  ASSERT_EQ(actual_format, DISK_FORMAT_MINFS);

  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  blk_fd.reset(open(fixture.block_device_path().c_str(), O_RDONLY));
  ASSERT_TRUE(blk_fd);
  actual_format = detect_disk_format(blk_fd.get());
  // Destroying the FVM should leave this in unknown format.
  ASSERT_EQ(actual_format, DISK_FORMAT_UNKNOWN);

  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);
  END_TEST;
}

bool SkipFormatIsOk() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_MINFS);
  options.isolated_devmgr = true;
  options.use_ramdisk = false;
  options.fs_format = false;

  // Create a Ramdisk which will be passed as the 'block_device'.
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(options.ramdisk_block_size, options.ramdisk_block_count, &ramdisk),
            ZX_OK);
  options.block_device_path = ramdisk_get_path(ramdisk);

  auto clean_up = fbl::MakeAutoCall([&ramdisk]() { ramdisk_destroy(ramdisk); });

  mkfs_options_t mkfs_options = default_mkfs_options;
  ASSERT_EQ(
      mkfs(options.block_device_path.c_str(), DISK_FORMAT_BLOBFS, launch_stdio_sync, &mkfs_options),
      ZX_OK);

  Fixture fixture(options);

  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  EXPECT_TRUE(options.block_device_path == fixture.block_device_path());
  EXPECT_TRUE(options.block_device_path == fixture.GetFsBlockDevice());
  fbl::unique_fd block_fd(open(fixture.block_device_path().c_str(), O_RDONLY));
  ASSERT_TRUE(block_fd);
  disk_format_t actual_format = detect_disk_format(block_fd.get());
  ASSERT_EQ(actual_format, DISK_FORMAT_BLOBFS);

  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  block_fd.reset(open(fixture.block_device_path().c_str(), O_RDONLY));
  ASSERT_TRUE(block_fd);
  actual_format = detect_disk_format(block_fd.get());
  ASSERT_EQ(actual_format, DISK_FORMAT_BLOBFS);

  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  block_fd.reset(open(fixture.block_device_path().c_str(), O_RDONLY));
  ASSERT_TRUE(block_fd);
  actual_format = detect_disk_format(block_fd.get());
  ASSERT_EQ(actual_format, DISK_FORMAT_BLOBFS);

  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);
  END_TEST;
}

bool SkipMountIsOk() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_MINFS);
  options.isolated_devmgr = true;
  options.fs_mount = false;

  Fixture fixture(options);
  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);

  // Verify nothing is mounted anymore.
  ASSERT_EQ(umount(fixture.fs_path().c_str()), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);
  END_TEST;
}

bool MountIsOk() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_MINFS);
  options.isolated_devmgr = true;
  options.fs_mount = false;

  Fixture fixture(options);
  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  ASSERT_EQ(umount(fixture.fs_path().c_str()), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fixture.Mount(), ZX_OK);
  ASSERT_EQ(umount(fixture.fs_path().c_str()), ZX_OK);

  // Since we need to try to umount to verify if the device is mounted,
  // the fixture still sees the device as mounted, so it will try to umount
  // and fail with not found, which is ok.
  ASSERT_EQ(fixture.TearDown(), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);
  END_TEST;
}

bool UmountIsOk() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_MINFS);
  options.fs_mount = true;
  options.isolated_devmgr = true;

  Fixture fixture(options);
  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  ASSERT_EQ(fixture.Umount(), ZX_OK);
  // Verify nothing is mounted anymore.
  ASSERT_EQ(umount(fixture.fs_path().c_str()), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);
  END_TEST;
}

bool RemountIsOk() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_MINFS);
  options.fs_mount = true;
  options.isolated_devmgr = true;

  Fixture fixture(options);
  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  ASSERT_EQ(fixture.Remount(), ZX_OK);
  ASSERT_EQ(umount(fixture.fs_path().c_str()), ZX_OK);
  // Teardown will return this error because we manually umount the underlying
  ASSERT_EQ(fixture.TearDown(), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);
  END_TEST;
}

bool FsckIsOk() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
  options.fs_mount = false;
  options.isolated_devmgr = true;

  Fixture fixture(options);
  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  // running fsck on a freshly formatted disk should never fail.
  ASSERT_EQ(fixture.Fsck(), ZX_OK);
  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);

  END_TEST;
}

bool FsckFails() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
  options.fs_mount = false;
  options.isolated_devmgr = true;

  Fixture fixture(options);
  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  // corrupt the disk!
  // right now we don't have a way to manipulate the internals of the
  // filesystem to get it into a corrupt state, so we take advantage of the
  // fact that we know where things are on disk and can just muck with them
  // directly. we just set a giant array to all 1 and squash the node map.
  uint8_t data[8192];
  memset(data, 0xffffffff, 8192);
  int dev = open(fixture.GetFsBlockDevice().c_str(), O_RDWR);
  if (dev < 0) {
    LOG_ERROR(0, "failed to open device\n");
  }
  ASSERT_EQ(sizeof(data), pwrite(dev, data, sizeof(data), sizeof(data)));
  int r3 = close(dev);
  if (r3 < 0) {
    LOG_ERROR(0, "failed to close\n");
  }
  sync();
  // fsck should fail...the filesystem is obviously corrupt!
  ASSERT_EQ(fixture.Fsck(), ZX_ERR_BAD_STATE);
  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);

  END_TEST;
}

bool FsckMounted() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
  options.isolated_devmgr = true;

  Fixture fixture(options);
  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  ASSERT_EQ(fixture.Fsck(), ZX_ERR_BAD_STATE);
  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);

  END_TEST;
}

bool FsckUnformatted() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
  options.fs_format = false;
  options.fs_mount = false;
  options.isolated_devmgr = true;

  Fixture fixture(options);
  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  ASSERT_EQ(fixture.Fsck(), ZX_ERR_BAD_STATE);
  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);

  END_TEST;
}

bool FsckNoBlockDevice() {
  BEGIN_TEST;
  FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
  options.use_ramdisk = false;
  options.fs_format = false;
  options.fs_mount = false;
  options.isolated_devmgr = true;

  Fixture fixture(options);
  ASSERT_EQ(fixture.SetUpTestCase(), ZX_OK);
  ASSERT_EQ(fixture.SetUp(), ZX_OK);
  ASSERT_EQ(fixture.Fsck(), ZX_ERR_BAD_STATE);
  ASSERT_EQ(fixture.TearDown(), ZX_OK);
  ASSERT_EQ(fixture.TearDownTestCase(), ZX_OK);

  END_TEST;
}

BEGIN_TEST_CASE(FixtureTest);
RUN_TEST(RamdiskSetupAndCleanup);
RUN_TEST(DiskIsFormattedCorrectlyNoFvm);
RUN_TEST(DiskAndFvmAreFormattedCorrectly);
RUN_TEST(UseBlockDeviceIsOk);
RUN_TEST(UseBlockDeviceWithFvmIsOk);
RUN_TEST(SkipFormatIsOk);
RUN_TEST(SkipMountIsOk);
RUN_TEST(MountIsOk);
RUN_TEST(UmountIsOk);
RUN_TEST(RemountIsOk);
RUN_TEST(FsckIsOk);
RUN_TEST(FsckFails);
RUN_TEST(FsckMounted);
RUN_TEST(FsckUnformatted);
RUN_TEST(FsckNoBlockDevice);
END_TEST_CASE(FixtureTest)

}  // namespace
}  // namespace fs_test_utils
