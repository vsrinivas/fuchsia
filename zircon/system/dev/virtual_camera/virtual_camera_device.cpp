// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtual_camera_device.h"
// #include "virtual_camera_stream.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

namespace virtual_camera {

zx_status_t VirtualCameraDevice::Create(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<VirtualCameraDevice>(&ac, parent);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device->DdkAdd("virtual_camera");
    if (status != ZX_OK) {
        zxlogf(
            ERROR,
            "virtual_camera_device: Could not create virtual camera device: %d\n",
            status);
        return status;
    }

    // device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto* dev = device.release();
    return ZX_OK;
}

void VirtualCameraDevice::DdkUnbind() {
    DdkRemove();
}

void VirtualCameraDevice::DdkRelease() {
    delete this;
}

zx_status_t VirtualCameraDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    zx_status_t status = fuchsia_hardware_camera_ControlV2_try_dispatch(
        this, txn, msg, &control_ops);
    return status;
}

void VirtualCameraDevice::RemoveStream(uint64_t stream_id) {
  streams_.erase(stream_id);
}

zx_status_t VirtualCameraDevice::GetFormats(uint32_t index, fidl_txn_t* txn) {
    fuchsia_hardware_camera_VideoFormat formats[16];

    fuchsia_hardware_camera_VideoFormat format = {
        .format = {.width = 640,
                   .height = 480,
                   .layers = 1,
                   .pixel_format =
                       {
                           .type = fuchsia_sysmem_PixelFormatType_BGRA32,
                           .has_format_modifier = false,
                           .format_modifier = {.value = 0},
                       },
                   .color_space = {.type = fuchsia_sysmem_ColorSpaceType_SRGB},
                   .planes = {{.byte_offset = 0, .bytes_per_row = 4 * 640},
                              {.byte_offset = 0, .bytes_per_row = 4 * 640},
                              {.byte_offset = 0, .bytes_per_row = 4 * 640},
                              {.byte_offset = 0, .bytes_per_row = 4 * 640}}},
        .rate =
            {
                .frames_per_sec_numerator = 30,
                .frames_per_sec_denominator = 1,
            },
    };

    formats[0] = format;
    return fuchsia_hardware_camera_ControlGetFormats_reply(txn, formats, 1, 1,
                                                           ZX_OK);
}

zx_status_t VirtualCameraDevice::CreateStream(
    const fuchsia_sysmem_BufferCollectionInfo* buffer_collection_info,
    const fuchsia_camera_common_FrameRate* rate, zx_handle_t stream,
    zx_handle_t stream_token) {
    zx::eventpair stream_event_token = zx::eventpair(stream_token);
    zx::channel stream_channel = zx::channel(stream);
    fbl::AllocChecker ac;
    auto new_stream = fbl::make_unique_checked<VirtualCameraStream>(
        &ac, this, count_, std::move(stream_event_token));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = new_stream->Bind(async_get_default_dispatcher(), std::move(stream_channel));
    if (status != ZX_OK) {
      return status;
    }

    status = new_stream->Init(buffer_collection_info);
    if (status != ZX_OK) {
      return status;
    }

    streams_[count_] = std::move(new_stream);
    count_++;
    return status;
}

zx_status_t VirtualCameraDevice::GetDeviceInfo(fidl_txn_t* txn) {
    fuchsia_hardware_camera_DeviceInfo info;
    info.output_capabilities = fuchsia_hardware_camera_CAMERA_OUTPUT_STREAM;
    info.max_stream_count = 1;
    return fuchsia_hardware_camera_ControlGetDeviceInfo_reply(txn, &info);
}

static zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = VirtualCameraDevice::Create;
    return ops;
}();

} // namespace virtual_camera

// clang-format off
ZIRCON_DRIVER_BEGIN(virtual_camera, virtual_camera::driver_ops, "vcamera", "0.1", 3)
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_VCAMERA_TEST),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_VCAMERA),
ZIRCON_DRIVER_END(virtual_camera)
// clang-format on
