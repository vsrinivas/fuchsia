// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtual_camera_stream.h"
#include "virtual_camera_device.h"


#include <ddk/debug.h>
#include <lib/zx/vmo.h>

namespace virtual_camera {

zx_status_t VirtualCameraStream::Init(
    const fuchsia_sysmem_BufferCollectionInfo* buffer_collection_info) {
    uint32_t buffer_count = buffer_collection_info->buffer_count;
    zx::vmo vmos[64];
    for (uint32_t i = 0; i < buffer_count; ++i) {
        vmos[i] = zx::vmo(buffer_collection_info->vmos[i]);
    }

    zx_status_t status = buffers_.Init(vmos, buffer_count);
    if (status != ZX_OK) {
        zxlogf(ERROR, "virtual_camera_stream: Error initializing buffer collection");
        return status;
    }

    stream_token_waiter_ = std::make_unique<async::Wait>(
        stream_token_.release(), ZX_EVENTPAIR_PEER_CLOSED, std::bind([this]() {
            if (is_streaming_) {
                Stop();
            }
            stream_token_.reset();
            stream_token_waiter_.reset();
            controller_->RemoveStream(stream_id_);
        }));

    status = stream_token_waiter_->Begin(async_get_default_dispatcher());
    if (status != ZX_OK) {
        // The waiter, dispatcher and token are known to be valid, so this should never happen.
        zxlogf(ERROR, "virtual_camera_stream: Error beginning Wait");
        return status;
    }
    return ZX_OK;
}

zx_status_t VirtualCameraStream::Start() {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtualCameraStream::Stop() {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtualCameraStream::ReleaseFrame(uint32_t index) {
    return ZX_ERR_NOT_SUPPORTED;
}

} // namespace virtual_camera
