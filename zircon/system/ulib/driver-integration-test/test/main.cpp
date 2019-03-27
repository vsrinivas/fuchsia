// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ddk/platform-defs.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/driver-integration-test/fixture.h>
#include <unittest/unittest.h>
#include <zircon/status.h>

using driver_integration_test::IsolatedDevmgr;
using devmgr_integration_test::RecursiveWaitForFile;

namespace {

const board_test::DeviceEntry kDeviceEntry = []() {
    board_test::DeviceEntry entry = {};
    strcpy(entry.name, "fallback-rtc");
    entry.vid = PDEV_VID_GENERIC;
    entry.pid = PDEV_PID_GENERIC;
    entry.did = PDEV_DID_RTC_FALLBACK;
    return entry;
}();

bool enumeration_test() {
    BEGIN_TEST;

    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.device_list.push_back(kDeviceEntry);

    IsolatedDevmgr devmgr;
    ASSERT_EQ(IsolatedDevmgr::Create(&args, &devmgr), ZX_OK);

    fbl::unique_fd fd;
    ASSERT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);

    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/test-board",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);

    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/00:00:f/fallback-rtc",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(driver_integration_tests)
RUN_TEST(enumeration_test)
END_TEST_CASE(driver_integration_tests)
