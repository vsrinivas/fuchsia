// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "virtual_camera_stream.h"

#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fuchsia/hardware/camera/c/fidl.h>
#include <fuchsia/camera/common/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/async/wait.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/vmo-pool.h>

namespace virtual_camera {

class VirtualCameraDevice;
using VirtualCameraDeviceType =
    ddk::Device<VirtualCameraDevice, ddk::Unbindable, ddk::Messageable>;

class VirtualCameraDevice : public VirtualCameraDeviceType,
                            public ddk::EmptyProtocol<ZX_PROTOCOL_CAMERA> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VirtualCameraDevice);

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    VirtualCameraDevice(zx_device_t* parent)
        : VirtualCameraDeviceType(parent), count_(0) {
    }

    ~VirtualCameraDevice() {}

    // DDK device implementation
    void DdkRelease();
    void DdkUnbind();
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

    void RemoveStream(uint64_t);


private:
    // DDK Helper Functions.
    zx_status_t GetFormats(uint32_t index, fidl_txn_t* txn);
    zx_status_t CreateStream(
        const fuchsia_sysmem_BufferCollectionInfo* buffer_collection_info,
        const fuchsia_camera_common_FrameRate* rate, zx_handle_t stream,
        zx_handle_t stream_token);
    zx_status_t GetDeviceInfo(fidl_txn_t* txn);

    static constexpr fuchsia_hardware_camera_ControlV2_ops_t control_ops = {
        .GetFormats = fidl::Binder<VirtualCameraDevice>::BindMember<
            &VirtualCameraDevice::GetFormats>,
        .CreateStream = fidl::Binder<VirtualCameraDevice>::BindMember<
            &VirtualCameraDevice::CreateStream>,
        .GetDeviceInfo = fidl::Binder<VirtualCameraDevice>::BindMember<
            &VirtualCameraDevice::GetDeviceInfo>,
    };

    std::map<uint64_t, fbl::unique_ptr<VirtualCameraStream>> streams_;

    // Variable to keep track of the number of streams created.
    uint64_t count_;

};

} // namespace virtual_camera
