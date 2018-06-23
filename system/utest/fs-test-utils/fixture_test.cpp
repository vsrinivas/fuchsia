// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include <fcntl.h>
#include <stdio.h>

#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fs-management/ramdisk.h>
#include <fs-test-utils/fixture.h>
#include <unittest/unittest.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>

namespace fs_test_utils {
namespace {

zx_status_t
GetBlockDeviceInfo(const fbl::String& block_device_path, block_info_t* blk_info) {
    fbl::unique_fd fd(open(block_device_path.c_str(), O_RDONLY));
    ssize_t result = ioctl_block_get_info(fd.get(), blk_info);
    if (result < 0) {
        return static_cast<zx_status_t>(result);
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
END_TEST_CASE(FixtureOptionsTests);

bool RamdiskSetupAndCleanup() {
    BEGIN_TEST;
    FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
    Fixture fixture(options);
    fixture.SetUpTestCase();
    ASSERT_TRUE(!fixture.block_device_path().empty());
    block_info_t ramdisk_info;
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

    // Create a Ramdisk which will be passed as the 'block_device'.
    char block_device[kPathSize];
    ASSERT_EQ(create_ramdisk(options.ramdisk_block_size,
                             options.ramdisk_block_count, block_device),
              ZX_OK);
    options.block_device_path = block_device;

    auto clean_up = fbl::MakeAutoCall([&options]() {
        destroy_ramdisk(options.block_device_path.c_str());
    });

    mkfs_options_t mkfs_options = default_mkfs_options;
    ASSERT_EQ(mkfs(options.block_device_path.c_str(), DISK_FORMAT_BLOBFS,
                   launch_stdio_sync, &mkfs_options),
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

    // Create a Ramdisk which will be passed as the 'block_device'.
    char block_device[kPathSize];
    ASSERT_EQ(create_ramdisk(options.ramdisk_block_size,
                             options.ramdisk_block_count, block_device),
              ZX_OK);
    options.block_device_path = block_device;

    auto clean_up = fbl::MakeAutoCall([&options]() {
        destroy_ramdisk(options.block_device_path.c_str());
    });

    mkfs_options_t mkfs_options = default_mkfs_options;
    ASSERT_EQ(mkfs(options.block_device_path.c_str(), DISK_FORMAT_BLOBFS,
                   launch_stdio_sync, &mkfs_options),
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

BEGIN_TEST_CASE(FixtureTest);
RUN_TEST(RamdiskSetupAndCleanup);
RUN_TEST(DiskIsFormattedCorrectlyNoFvm);
RUN_TEST(DiskAndFvmAreFormattedCorrectly);
RUN_TEST(UseBlockDeviceIsOk);
RUN_TEST(UseBlockDeviceWithFvmIsOk);
END_TEST_CASE(FixtureTest);

} // namespace
} // namespace fs_test_utils
