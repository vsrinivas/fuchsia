// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <fuchsia/hardware/camera/c/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;

namespace {

// Integration test for the driver defined in zircon/system/dev/virtual_camera.
// This test code loads the driver into an isolated devmgr and tests behavior.
class VirtualCameraTest : public zxtest::Test {
    void SetUp() override;

protected:
    IsolatedDevmgr devmgr_;
    fbl::unique_fd fd_;
    zx_handle_t handle_;
};

const board_test::DeviceEntry kDeviceEntry = []() {
    board_test::DeviceEntry entry = {};
    entry.vid = PDEV_VID_TEST;
    entry.pid = PDEV_PID_VCAMERA_TEST;
    entry.did = PDEV_DID_TEST_VCAMERA;
    return entry;
}();

void VirtualCameraTest::SetUp() {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.driver_search_paths.push_back("/boot/driver/test");
    args.device_list.push_back(kDeviceEntry);
    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr_);
    ASSERT_EQ(status, ZX_OK);

    status = devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:05:b/virtual_camera",
        zx::time::infinite(), &fd_);
    ASSERT_EQ(ZX_OK, status);

    status = fdio_get_service_handle(fd_.get(), &handle_);
    ASSERT_EQ(ZX_OK, status);
}

TEST_F(VirtualCameraTest, GetDeviceInfoGetFormatsTest) {
    fuchsia_hardware_camera_DeviceInfo device_info;
    zx_status_t status =
        fuchsia_hardware_camera_ControlGetDeviceInfo(handle_, &device_info);
    ASSERT_EQ(ZX_OK, status);
    EXPECT_EQ(1, device_info.max_stream_count);
    EXPECT_EQ(fuchsia_hardware_camera_CAMERA_OUTPUT_STREAM,
              device_info.output_capabilities);

    fuchsia_hardware_camera_VideoFormat formats[16];
    uint32_t total_count;
    uint32_t actual_count;
    int32_t out_status;
    status = fuchsia_hardware_camera_ControlGetFormats(
        handle_, 1, formats, &total_count, &actual_count, &out_status);
    ASSERT_EQ(ZX_OK, status);
    auto format = formats[0];
    EXPECT_EQ(640, format.format.width);
    EXPECT_EQ(480, format.format.height);
    EXPECT_EQ(1, format.format.layers);
    EXPECT_EQ(30, format.rate.frames_per_sec_numerator);
    EXPECT_EQ(1, format.rate.frames_per_sec_denominator);
}

} // namespace
