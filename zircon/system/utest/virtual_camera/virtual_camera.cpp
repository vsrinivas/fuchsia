// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <fuchsia/camera/common/c/fidl.h>
#include <fuchsia/hardware/camera/c/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/vmo.h>
#include <zxtest/zxtest.h>
#include <zircon/syscalls.h>


using driver_integration_test::IsolatedDevmgr;

namespace {

constexpr uint32_t kPageSize = 4096;

// TODO(CAM-43): Replace with sysmem version when available?
zx_status_t Gralloc(fuchsia_camera_common_VideoFormat format, uint32_t num_buffers,
                    fuchsia_sysmem_BufferCollectionInfo* buffer_collection,
                    zx::vmo vmos[64]) {
    // In the future, some special alignment might happen here, or special
    // memory allocated...
    // Simple GetBufferSize.  Only valid for simple formats:
    size_t buffer_size = fbl::round_up(
        format.format.height * format.format.planes[0].bytes_per_row, kPageSize);
    buffer_collection->buffer_count = num_buffers;
    buffer_collection->vmo_size = buffer_size;
    buffer_collection->format.image = format.format;
    zx_status_t status;
    for (uint32_t i = 0; i < num_buffers; ++i) {
        status = zx::vmo::create(buffer_size, 0, &vmos[i]);
        if (status != ZX_OK) {
            return status;
        }
        buffer_collection->vmos[i] = vmos[i].get();
    }
    return ZX_OK;
}

// Integration test for the driver defined in zircon/system/dev/virtual_camera.
// This test code loads the driver into an isolated devmgr and tests behavior.
class VirtualCameraTest : public zxtest::Test {
    void SetUp() override;

protected:
    IsolatedDevmgr devmgr_;
    fbl::unique_fd fd_;
    zx_handle_t device_handle_;
    fuchsia_sysmem_BufferCollectionInfo info_0_;
    zx::vmo vmos_0_[64];
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

    status = fdio_get_service_handle(fd_.get(), &device_handle_);
    ASSERT_EQ(ZX_OK, status);
}

TEST_F(VirtualCameraTest, GetDeviceInfoGetFormatsTest) {
    fuchsia_hardware_camera_DeviceInfo device_info;
    zx_status_t status =
        fuchsia_hardware_camera_ControlV2GetDeviceInfo(device_handle_, &device_info);
    ASSERT_EQ(ZX_OK, status);
    EXPECT_EQ(1, device_info.max_stream_count);
    EXPECT_EQ(fuchsia_hardware_camera_CAMERA_OUTPUT_STREAM,
              device_info.output_capabilities);

    fuchsia_camera_common_VideoFormat formats[16];
    uint32_t total_count;
    uint32_t actual_count;
    int32_t out_status;
    status = fuchsia_hardware_camera_ControlV2GetFormats(
        device_handle_, 1, formats, &total_count, &actual_count, &out_status);
    ASSERT_EQ(ZX_OK, status);
    auto format = formats[0];
    EXPECT_EQ(640, format.format.width);
    EXPECT_EQ(480, format.format.height);
    EXPECT_EQ(1, format.format.layers);
    EXPECT_EQ(30, format.rate.frames_per_sec_numerator);
    EXPECT_EQ(1, format.rate.frames_per_sec_denominator);
    EXPECT_EQ(1, total_count);
    EXPECT_EQ(1, actual_count);

    zx::eventpair driver_token;
    zx::eventpair stream_token;
    status = zx::eventpair::create(0, &stream_token, &driver_token);
    ASSERT_EQ(ZX_OK, status);

    zx::channel client_request;
    zx::channel server_request;
    status = zx::channel::create(0, &client_request, &server_request);
    ASSERT_EQ(ZX_OK, status);
    status = Gralloc(format, 2, &info_0_, vmos_0_);
    ASSERT_EQ(ZX_OK, status);
    status = fuchsia_hardware_camera_ControlV2CreateStream(
        device_handle_, &info_0_, &format.rate, server_request.release(), driver_token.release());
    ASSERT_EQ(ZX_OK, status);

    // Not fully implemented yet - this is a sanity check.
    zx_handle_t stream_handle = client_request.release();
    status = fuchsia_camera_common_StreamStart(stream_handle);
    EXPECT_EQ(ZX_OK, status);

    stream_token.reset();
    zx_signals_t pending;
    zx::time deadline = zx::deadline_after(zx::sec(5));
    zx::channel client = zx::channel(stream_handle);
    ASSERT_EQ(ZX_OK, client.wait_one(ZX_CHANNEL_PEER_CLOSED, deadline, &pending));
    ASSERT_EQ(pending & ZX_CHANNEL_PEER_CLOSED, ZX_CHANNEL_PEER_CLOSED);
}

} // namespace
