// Copyright 2018 The Fuchsia Authors. All rights reserved.
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
#include <lib/devmgr-launcher/launch.h>
#include <lib/zx/vmo.h>
#include <libzbi/zbi-cpp.h>
#include <unittest/unittest.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

using devmgr_integration_test::IsolatedDevmgr;
using devmgr_integration_test::RecursiveWaitForFile;

namespace {

zbi_platform_id_t kPlatformId = [](){
    zbi_platform_id_t plat_id = {};
    plat_id.vid = PDEV_VID_TEST;
    plat_id.pid = PDEV_PID_PBUS_TEST;
    strcpy(plat_id.board_name, "pbus-test");
    return plat_id;
}();

zx_status_t GetBootItem(uint32_t type, uint32_t extra, zx::vmo* out, uint32_t* length) {
    if (type != ZBI_TYPE_PLATFORM_ID) {
        return ZX_OK;
    }
    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(sizeof(kPlatformId), 0, &vmo);
    if (status != ZX_OK) {
        return status;
    }
    status = vmo.write(&kPlatformId, 0, sizeof(kPlatformId));
    if (status != ZX_OK) {
        return status;
    }
    *out = std::move(vmo);
    return ZX_OK;
}

bool enumeration_test() {
    BEGIN_TEST;

    devmgr_launcher::Args args;
    args.sys_device_driver = "/boot/driver/platform-bus.so";
    args.driver_search_paths.push_back("/boot/driver");
    args.get_boot_item = GetBootItem;

    IsolatedDevmgr devmgr;
    ASSERT_EQ(IsolatedDevmgr::Create(std::move(args), &devmgr), ZX_OK);

    fbl::unique_fd fd;
    ASSERT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);

    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/test-board",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);

    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:1",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);

    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:1/child-1",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);

    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:1/child-1/child-2-top",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);
    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(),
                                   "sys/platform/11:01:1/child-1/child-2-top/child-2",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);
    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/11:01:1/child-1/child-3-top",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);
    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(),
                                   "sys/platform/11:01:1/child-1/child-3-top/child-3",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);

    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(),
                                   "sys/platform/11:01:5/test-gpio/gpio-3/component",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);
    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(),
                                   "sys/platform/11:01:1/child-1/child-3-top/child-3/component",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);
    EXPECT_EQ(RecursiveWaitForFile(devmgr.devfs_root(),
                                   "composite-dev/composite",
                                   zx::deadline_after(zx::sec(5)), &fd),
              ZX_OK);

    const int dirfd = devmgr.devfs_root().get();
    struct stat st;
    EXPECT_EQ(fstatat(dirfd, "sys/platform/test-board", &st, 0), 0);
    EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1", &st, 0), 0);
    EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1", &st, 0), 0);
    EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-2-top", &st, 0), 0);
    EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-3-top", &st, 0), 0);
    EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-2-top/child-2", &st, 0), 0);
    EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-3-top/child-3", &st, 0), 0);
    EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:5/test-gpio/gpio-3/component", &st, 0),
              0);
   EXPECT_EQ(fstatat(dirfd, "sys/platform/11:01:1/child-1/child-3-top/child-3/component", &st, 0),
              0);
    EXPECT_EQ(fstatat(dirfd, "composite-dev/composite", &st, 0), 0);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(pbus_tests)
RUN_TEST(enumeration_test)
END_TEST_CASE(pbus_tests);
