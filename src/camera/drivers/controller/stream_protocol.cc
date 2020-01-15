// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream_protocol.h"

#include <utility>

#include <fbl/auto_lock.h>

#include "processing_node.h"
#include "src/lib/syslog/cpp/logger.h"

namespace camera {

constexpr auto kTag = "camera_controller";

StreamImpl::StreamImpl(async_dispatcher_t* dispatcher, ProcessNode* output_node)
    : dispatcher_(dispatcher), binding_(this), output_node_(*output_node) {}

zx_status_t StreamImpl::Attach(zx::channel channel, fit::function<void(void)> disconnect_handler) {
  FX_DCHECK(!binding_.is_bound());
  disconnect_handler_ = std::move(disconnect_handler);
  binding_.set_error_handler([this](zx_status_t status) {
    Shutdown(status);
    disconnect_handler_();
  });

  zx_status_t status = binding_.Bind(std::move(channel));
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status);
    return status;
  }
  return ZX_OK;
}

void StreamImpl::FrameReady(const frame_available_info_t* info) {
  // This method is invoked by the ISP in its own thread, so the event must be marshalled to the
  // binding's thread.
  fbl::AutoLock guard(&event_queue_lock_);
  event_queue_.emplace([this, info = *info]() {
    fuchsia::camera2::FrameAvailableInfo frame_info;
    frame_info.frame_status = fuchsia::camera2::FrameStatus::OK;
    frame_info.buffer_id = info.buffer_id;
    frame_info.metadata.set_image_format_index(info.metadata.image_format_index);
    frame_info.metadata.set_timestamp(info.metadata.timestamp);
    binding_.events().OnFrameAvailable(std::move(frame_info));
    fbl::AutoLock guard(&event_queue_lock_);
    event_queue_.pop();
  });
  event_queue_.back().Post(dispatcher_);
}

void StreamImpl::Shutdown(zx_status_t status) {
  // Close the connection if it's open.
  if (binding_.is_bound()) {
    binding_.Close(status);
  }

  // Stop streaming if its started
  if (started_) {
    Stop();
  }
}

void StreamImpl::Stop() {
  if (!started_) {
    Shutdown(ZX_ERR_BAD_STATE);
    return;
  }

  output_node_.OnStopStreaming();
  started_ = false;
}

void StreamImpl::Start() {
  if (started_) {
    Shutdown(ZX_ERR_BAD_STATE);
    return;
  }
  output_node_.OnStartStreaming();
  started_ = true;
}

void StreamImpl::ReleaseFrame(uint32_t buffer_id) { output_node_.OnReleaseFrame(buffer_id); }

void StreamImpl::AcknowledgeFrameError() {
  FX_LOGST(ERROR, kTag) << __PRETTY_FUNCTION__ << " not implemented";
  Shutdown(ZX_ERR_UNAVAILABLE);
}

void StreamImpl::SetRegionOfInterest(float x_min, float y_min, float x_max, float y_max,
                                     SetRegionOfInterestCallback callback) {
  FX_LOGST(ERROR, kTag) << __PRETTY_FUNCTION__ << " not implemented";
  Shutdown(ZX_ERR_UNAVAILABLE);
}

void StreamImpl::SetImageFormat(uint32_t image_format_index, SetImageFormatCallback callback) {
  FX_LOGST(ERROR, kTag) << __PRETTY_FUNCTION__ << " not implemented";
  Shutdown(ZX_ERR_UNAVAILABLE);
}

void StreamImpl::GetImageFormats(GetImageFormatsCallback callback) {
  FX_LOGST(ERROR, kTag) << __PRETTY_FUNCTION__ << " not implemented";
  Shutdown(ZX_ERR_UNAVAILABLE);
}

}  // namespace camera
