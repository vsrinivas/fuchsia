// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream_protocol.h"

#include <fbl/auto_lock.h>

#include "src/lib/fxl/logging.h"

camera::StreamImpl::StreamImpl(async_dispatcher_t* dispatcher,
                               std::unique_ptr<camera::IspStreamProtocol> isp_stream_protocol)
    : dispatcher_(dispatcher),
      binding_(this),
      callbacks_{FrameReady, this},
      isp_stream_protocol_(std::move(isp_stream_protocol)) {}

camera::StreamImpl::~StreamImpl() { Shutdown(ZX_OK); }

zx_status_t camera::StreamImpl::Attach(zx::channel channel,
                                       fit::function<void(void)> disconnect_handler) {
  FXL_DCHECK(!binding_.is_bound());
  disconnect_handler_ = std::move(disconnect_handler);
  binding_.set_error_handler([this](zx_status_t status) {
    FXL_PLOG(ERROR, status) << "Client disconnected";
    Shutdown(status);
    disconnect_handler_();
  });
  zx_status_t status = binding_.Bind(std::move(channel));
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status);
    return status;
  }
  return ZX_OK;
}

void camera::StreamImpl::FrameReady(uint32_t buffer_id) {
  // This method is invoked by the ISP in its own thread, so the event must be marshalled to the
  // binding's thread.
  fbl::AutoLock guard(&event_queue_lock_);
  event_queue_.emplace([this, buffer_id]() {
    held_buffers_.insert(buffer_id);
    fuchsia::camera2::FrameAvailableInfo info;
    info.frame_status = fuchsia::camera2::FrameStatus::OK;
    info.buffer_id = buffer_id;
    binding_.events().OnFrameAvailable(std::move(info));
    fbl::AutoLock guard(&event_queue_lock_);
    event_queue_.pop();
  });
  event_queue_.back().Post(dispatcher_);
}

void camera::StreamImpl::Shutdown(zx_status_t status) {
  // Close the connection if it's open.
  if (binding_.is_bound()) {
    binding_.Close(status);
  }

  // Stop streaming ISP
  if (started_) {
    isp_stream_protocol_->Stop();
  }
}

void camera::StreamImpl::Stop() {
  if (!started_) {
    Shutdown(ZX_ERR_BAD_STATE);
    return;
  }

  isp_stream_protocol_->Stop();
  started_ = false;
}

void camera::StreamImpl::Start() {
  if (started_) {
    Shutdown(ZX_ERR_BAD_STATE);
    return;
  }
  isp_stream_protocol_->Start();
  started_ = true;
}

void camera::StreamImpl::ReleaseFrame(uint32_t buffer_id) {
  isp_stream_protocol_->ReleaseFrame(buffer_id);
}

void camera::StreamImpl::AcknowledgeFrameError() {
  FXL_LOG(ERROR) << __PRETTY_FUNCTION__ << " not implemented";
  Shutdown(ZX_ERR_UNAVAILABLE);
}

void camera::StreamImpl::SetRegionOfInterest(float x_min, float y_min, float x_max, float y_max,
                                             SetRegionOfInterestCallback callback) {
  FXL_LOG(ERROR) << __PRETTY_FUNCTION__ << " not implemented";
  Shutdown(ZX_ERR_UNAVAILABLE);
}

void camera::StreamImpl::SetImageFormat(uint32_t image_format_index,
                                        SetImageFormatCallback callback) {
  FXL_LOG(ERROR) << __PRETTY_FUNCTION__ << " not implemented";
  Shutdown(ZX_ERR_UNAVAILABLE);
}

void camera::StreamImpl::GetImageFormats(GetImageFormatsCallback callback) {
  FXL_LOG(ERROR) << __PRETTY_FUNCTION__ << " not implemented";
  Shutdown(ZX_ERR_UNAVAILABLE);
}
