// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/camera/common/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/vmo-pool.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>

namespace virtual_camera {

class VirtualCameraDevice;

class VirtualCameraStream {
  public:
    VirtualCameraStream(VirtualCameraDevice* controller, uint64_t stream_id, zx::eventpair stream_token)
        : stream_id_(stream_id), controller_(controller), stream_token_(stream_token.release()) {}

    ~VirtualCameraStream() {}

    zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel stream) {
        static constexpr fuchsia_camera_common_Stream_ops_t kOps = {
            .Start = fidl::Binder<VirtualCameraStream>::BindMember<
                &VirtualCameraStream::Start>,
            .Stop = fidl::Binder<VirtualCameraStream>::BindMember<
                &VirtualCameraStream::Stop>,
            .ReleaseFrame = fidl::Binder<VirtualCameraStream>::BindMember<
                &VirtualCameraStream::ReleaseFrame>,
        };
        return fidl::Binder<VirtualCameraStream>::BindOps<fuchsia_camera_common_Stream_dispatch>(
            dispatcher, std::move(stream), this, &kOps);
    }

    zx_status_t Init(const fuchsia_sysmem_BufferCollectionInfo* buffer_collection_info);

  private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VirtualCameraStream);

    bool is_streaming_;
    uint64_t stream_id_;

    zx_status_t Start();
    zx_status_t Stop();
    zx_status_t ReleaseFrame(uint32_t buffer_id);

    VirtualCameraDevice* controller_;

    fzl::VmoPool buffers_;
    // The stream waits on the client to release their token to shutdown.
    zx::eventpair stream_token_;
    std::unique_ptr<async::Wait> stream_token_waiter_;
};

} // namespace virtual_camera
