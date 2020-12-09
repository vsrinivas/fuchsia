// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <sstream>

#include "src/camera/bin/device/messages.h"
#include "src/camera/bin/device/stream_impl.h"
#include "src/lib/fsl/handles/object_info.h"

StreamImpl::Client::Client(StreamImpl& stream, uint64_t id,
                           fidl::InterfaceRequest<fuchsia::camera3::Stream> request)
    : stream_(stream),
      id_(id),
      binding_(this, std::move(request)),
      resolution_([](fuchsia::math::Size a, fuchsia::math::Size b) {
        return (a.width == b.width) && (a.height == b.height);
      }) {
  FX_LOGS(DEBUG) << "Stream client " << id << " connected.";
  binding_.set_error_handler(fit::bind_member(this, &StreamImpl::Client::OnClientDisconnected));
}

StreamImpl::Client::~Client() = default;

void StreamImpl::Client::AddFrame(fuchsia::camera3::FrameInfo2 frame) {
  TRACE_DURATION("camera", "StreamImpl::Client::AddFrame");
  frames_.push(std::move(frame));
  MaybeSendFrame();
}

void StreamImpl::Client::MaybeSendFrame() {
  TRACE_DURATION("camera", "StreamImpl::Client::MaybeSendFrame");
  if (frames_.empty() || !frame_callback_) {
    return;
  }
  auto& frame = frames_.front();
  // This Flow can be connected on the client end to allow tracing the flow of frames
  // into the client.
  TRACE_FLOW_BEGIN("camera", "camera3::Stream::GetNextFrame",
                   fsl::GetKoid(frame.release_fence().get()));
  frame_callback_(std::move(frame));
  frames_.pop();
  frame_callback_ = nullptr;
}

void StreamImpl::Client::ReceiveBufferCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  TRACE_DURATION("camera", "StreamImpl::Client::ReceiveBufferCollection");
  buffers_.Set(std::move(token));
}

void StreamImpl::Client::ReceiveResolution(fuchsia::math::Size coded_size) {
  TRACE_DURATION("camera", "StreamImpl::Client::ReceiveResolution");
  resolution_.Set(coded_size);
}

void StreamImpl::Client::ReceiveCropRegion(std::unique_ptr<fuchsia::math::RectF> region) {
  TRACE_DURATION("camera", "StreamImpl::Client::ReceiveCropRegion");
  // TODO(fxbug.dev/51176): Because unique_ptr is non-copyable, the hanging get helper assumes all
  // values are never the same as previous values. In this case, however, back-to-back null regions
  // or identical regions should not be sent twice.
  crop_region_.Set(std::move(region));
}

bool& StreamImpl::Client::Participant() { return participant_; }

void StreamImpl::Client::ClearFrames() {
  while (!frames_.empty()) {
    frames_.pop();
  }
}

void StreamImpl::Client::OnClientDisconnected(zx_status_t status) {
  FX_PLOGS(DEBUG, status) << "Stream client " << id_ << " disconnected.";
  stream_.RemoveClient(id_);
}

void StreamImpl::Client::CloseConnection(zx_status_t status) {
  binding_.Close(status);
  stream_.RemoveClient(id_);
}

void StreamImpl::Client::GetProperties(GetPropertiesCallback callback) {
  TRACE_DURATION("camera", "StreamImpl::Client::GetProperties");
  callback(Convert(stream_.properties_));
}

void StreamImpl::Client::GetProperties2(GetProperties2Callback callback) {
  TRACE_DURATION("camera", "StreamImpl::Client::GetProperties2");
  callback(fidl::Clone(stream_.properties_));
}

void StreamImpl::Client::SetCropRegion(std::unique_ptr<fuchsia::math::RectF> region) {
  TRACE_DURATION("camera", "StreamImpl::Client::SetCropRegion");
  if (!stream_.properties_.supports_crop_region()) {
    CloseConnection(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  if (region && (region->x < 0.0f || region->y < 0.0f || region->x + region->width > 1.0f ||
                 region->y + region->height > 1.0f)) {
    FX_LOGS(INFO) << "Client requested invalid crop region {" << region->x << ", " << region->y
                  << ", " << region->width << ", " << region->height << "}";
    CloseConnection(ZX_ERR_INVALID_ARGS);
    return;
  }

  stream_.SetCropRegion(id_, std::move(region));
}

void StreamImpl::Client::WatchCropRegion(WatchCropRegionCallback callback) {
  TRACE_DURATION("camera", "StreamImpl::Client::WatchCropRegion");
  if (crop_region_.Get(std::move(callback))) {
    CloseConnection(ZX_ERR_BAD_STATE);
  }
}

void StreamImpl::Client::SetResolution(fuchsia::math::Size coded_size) {
  TRACE_DURATION("camera", "StreamImpl::Client::SetResolution");
  stream_.SetResolution(id_, coded_size);
}

void StreamImpl::Client::WatchResolution(WatchResolutionCallback callback) {
  TRACE_DURATION("camera", "StreamImpl::Client::WatchResolution");
  if (resolution_.Get(std::move(callback))) {
    CloseConnection(ZX_ERR_BAD_STATE);
  }
}

void StreamImpl::Client::SetBufferCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  TRACE_DURATION("camera", "StreamImpl::Client::SetBufferCollection");
  stream_.SetBufferCollection(id_, std::move(token));
}

void StreamImpl::Client::WatchBufferCollection(WatchBufferCollectionCallback callback) {
  TRACE_DURATION("camera", "StreamImpl::Client::WatchBufferCollection");
  if (buffers_.Get(std::move(callback))) {
    CloseConnection(ZX_ERR_BAD_STATE);
  }
}

void StreamImpl::Client::WatchOrientation(WatchOrientationCallback callback) {
  TRACE_DURATION("camera", "StreamImpl::Client::WatchOrientation");
  // Orientation is not currently reported by hardware. Assume UP (no transform).
  callback(fuchsia::camera3::Orientation::UP);
}

void StreamImpl::Client::GetNextFrame(GetNextFrameCallback callback) {
  GetNextFrame2([callback = std::move(callback)](fuchsia::camera3::FrameInfo2 frame) {
    callback({.buffer_index = frame.buffer_index(),
              .frame_counter = frame.frame_counter(),
              .timestamp = frame.timestamp(),
              .release_fence = std::move(*frame.mutable_release_fence())});
  });
}

void StreamImpl::Client::GetNextFrame2(GetNextFrame2Callback callback) {
  TRACE_DURATION("camera", "StreamImpl::Client::GetNextFrame2");
  if (frame_callback_) {
    FX_LOGS(INFO) << "Client called GetNextFrame while a previous call was still pending.";
    CloseConnection(ZX_ERR_BAD_STATE);
    return;
  }
  frame_callback_ = std::move(callback);
  MaybeSendFrame();
}

void StreamImpl::Client::Rebind(fidl::InterfaceRequest<Stream> request) {
  stream_.OnNewRequest(std::move(request));
}
