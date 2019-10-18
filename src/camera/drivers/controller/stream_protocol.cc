// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream_protocol.h"

#include <fbl/auto_lock.h>

#include "src/lib/fxl/logging.h"

camera::StreamImpl::StreamImpl(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher),
      binding_(this),
      callbacks_{FrameReady, this},
      protocol_{&protocol_ops_, nullptr} {}

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

  // Stop the ISP stream if it is started.
  if (started_) {
    zx_status_t status = protocol_.ops->stop(protocol_.ctx);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status);
    }
    started_ = false;
  }

  // Release any client-leaked buffers.
  for (auto id : held_buffers_) {
    zx_status_t status = protocol_.ops->release_frame(protocol_.ctx, id);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status);
    }
  }
  held_buffers_.clear();
}

void camera::StreamImpl::Start() {
  if (started_) {
    FXL_LOG(ERROR) << "It is invalid to call Start on a Stream that is already started";
    Shutdown(ZX_ERR_BAD_STATE);
    return;
  }
  started_ = true;

  zx_status_t status = protocol_.ops->start(protocol_.ctx);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status);
    Shutdown(ZX_ERR_INTERNAL);
    return;
  }
}

void camera::StreamImpl::Stop() {
  if (!started_) {
    FXL_LOG(ERROR) << "It is invalid to call Stop on a Stream that is already stopped";
    Shutdown(ZX_ERR_BAD_STATE);
    return;
  }
  started_ = false;

  zx_status_t status = protocol_.ops->stop(protocol_.ctx);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status);
    Shutdown(ZX_ERR_INTERNAL);
    return;
  }
}

void camera::StreamImpl::ReleaseFrame(uint32_t buffer_id) {
  auto it = held_buffers_.find(buffer_id);
  if (it == held_buffers_.end()) {
    FXL_LOG(ERROR) << "Client attempted to release unowned buffer " << buffer_id;
    Shutdown(ZX_ERR_INVALID_ARGS);
    return;
  }

  zx_status_t status = protocol_.ops->release_frame(protocol_.ctx, buffer_id);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status);
    Shutdown(ZX_ERR_INTERNAL);
  }
  held_buffers_.erase(it);
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
