// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include <fcntl.h>
#include <stdio.h>

#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>
#include <fs-test-utils/fixture.h>
#include <fs-test-utils/unittest.h>
#include <unittest/unittest.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>

namespace fs_test_utils {
namespace {

FixtureOptions OptionsUseRamdiskAndFvm() {
    FixtureOptions options = FixtureOptions::Default(DISK_FORMAT_MINFS);
    options.use_fvm = true;
    options.fs_type = DISK_FORMAT_MINFS;
    return options;
}

FixtureOptions OptionsUseRamdiskAndFvm2() {
    return OptionsUseRamdiskAndFvm();
}

bool VerifyRamdiskAndFvmExist(Fixture* fixture) {
    BEGIN_TEST;
    ASSERT_FALSE(fixture->partition_path().empty(), "No partition path set");
    ASSERT_FALSE(fixture->block_device_path().empty(), "No block device path set.");
    ASSERT_FALSE(fixture->fs_path().empty(), "No fs_path set");

    fbl::unique_fd fs_path_fd(open(fixture->fs_path().c_str(), O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(fs_path_fd);

    fbl::unique_fd block_fd(open(fixture->block_device_path().c_str(), O_RDONLY));
    ASSERT_TRUE(block_fd);
    disk_format_t actual = detect_disk_format(block_fd.get());
    ASSERT_EQ(actual, DISK_FORMAT_FVM);

    fbl::unique_fd fs_fd(open(fixture->partition_path().c_str(), O_RDONLY));
    ASSERT_TRUE(fs_fd);
    actual = detect_disk_format(fs_fd.get());
    ASSERT_EQ(actual, fixture->options().fs_type);

    END_TEST;
}

bool VerifyRamdiskAndFvmExist2(Fixture* fixture) {
    return VerifyRamdiskAndFvmExist(fixture);
}

BEGIN_FS_TEST_CASE(UnittestFixtureTest, OptionsUseRamdiskAndFvm)
RUN_FS_TEST_F(VerifyRamdiskAndFvmExist)
END_FS_TEST_CASE(UnittestFixtureTest, OptionsUseRamdiskAndFvm)

// Verifies that we can define multiple test cases without collision on global symbols,
// and run multiple tests.
BEGIN_FS_TEST_CASE(UnittestFixtureTest, OptionsUseRamdiskAndFvm2)
RUN_FS_TEST_F(VerifyRamdiskAndFvmExist)
RUN_FS_TEST_F(VerifyRamdiskAndFvmExist)
RUN_FS_TEST_F(VerifyRamdiskAndFvmExist2)
END_FS_TEST_CASE(UnittestFixtureTest, OptionsUseRamdiskAndFvm2)

} // namespace
} // namespace fs_test_utils

namespace fs_test_utils_2 {
namespace {

fs_test_utils::FixtureOptions Options() {
    return fs_test_utils::OptionsUseRamdiskAndFvm();
}

BEGIN_FS_TEST_CASE(UnittestFixtureFromAnotherNamespaceTest, Options)
RUN_FS_TEST_F(fs_test_utils::VerifyRamdiskAndFvmExist)
END_FS_TEST_CASE(UnittestFixtureFromAnotherNamespaceTest, Options)
} // namespace

} // namespace fs_test_utils_2
