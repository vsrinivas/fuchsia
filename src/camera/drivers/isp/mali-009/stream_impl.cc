// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream_impl.h"

#include <ddktl/protocol/isp.h>
#include <fbl/auto_lock.h>

#include "src/lib/syslog/cpp/logger.h"

namespace camera {

constexpr auto TAG = "arm-isp";

zx_status_t StreamImpl::Create(zx::channel channel, async_dispatcher_t* dispatcher,
                               std::unique_ptr<StreamImpl>* stream_out) {
  auto stream = std::make_unique<StreamImpl>();
  zx_status_t status = stream->binding_.Bind(
      fidl::InterfaceRequest<fuchsia::camera2::Stream>(std::move(channel)), dispatcher);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "Failed to bind stream";
    return status;
  }

  *stream_out = std::move(stream);
  return ZX_OK;
}

void StreamImpl::FrameAvailable(uint32_t id) {
  if (!streaming_) {
    return;
  }
  fuchsia::camera2::FrameAvailableInfo event{};
  event.frame_status = fuchsia::camera2::FrameStatus::OK;
  event.buffer_id = id;
  binding_.events().OnFrameAvailable(std::move(event));
  outstanding_buffers_.insert(id);
}

void StreamImpl::Start() {
  if (streaming_) {
    FX_LOGST(ERROR, TAG) << "It is invalid to call Start on a stream that is already streaming.";
    binding_.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  streaming_ = true;
}

void StreamImpl::Stop() {
  if (!streaming_) {
    FX_LOGST(ERROR, TAG) << "It is invalid to call Stop on a stream that is stopped.";
    binding_.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  streaming_ = false;
}

void StreamImpl::ReleaseFrame(uint32_t buffer_id) {
  auto it = outstanding_buffers_.find(buffer_id);
  if (it == outstanding_buffers_.end()) {
    FX_LOGST(ERROR, TAG) << "Client attempted to release buffer " << buffer_id
                         << " but it was not previously held.";
    binding_.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  outstanding_buffers_.erase(it);
}

}  // namespace camera
