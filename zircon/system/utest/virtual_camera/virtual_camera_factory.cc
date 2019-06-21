// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;

namespace {

class VirtualManagerTest : public zxtest::Test {

protected:
    IsolatedDevmgr devmgr_;
    fbl::unique_fd fd_;
    zx_handle_t manager_handle_;
};

const board_test::DeviceEntry kDeviceEntry = []() {
    board_test::DeviceEntry entry = {};
    entry.vid = PDEV_VID_TEST;
    entry.pid = PDEV_PID_VCAMERA_TEST;
    entry.did = PDEV_DID_TEST_VCAM_FACTORY;
    return entry;
}();

TEST_F(VirtualManagerTest, DriverFoundTest) {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.driver_search_paths.push_back("/boot/driver/test");
    args.device_list.push_back(kDeviceEntry);
    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr_);
    ASSERT_OK(status);

    status = devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:05:e/virtual_camera_factory", &fd_);
    ASSERT_OK(status);

    status = fdio_get_service_handle(fd_.get(), &manager_handle_);
    ASSERT_OK(status);
}

} // namespace
