// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

#include <sstream>

#include "src/camera/bin/device/messages.h"
#include "src/camera/bin/device/stream_impl.h"

StreamImpl::Client::Client(StreamImpl& stream, uint64_t id,
                           fidl::InterfaceRequest<fuchsia::camera3::Stream> request)
    : stream_(stream),
      id_(id),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      binding_(this, std::move(request), loop_.dispatcher()),
      resolution_([](fuchsia::math::Size a, fuchsia::math::Size b) {
        return (a.width == b.width) && (a.height == b.height);
      }) {
  FX_LOGS(DEBUG) << "Stream client " << id << " connected.";
  binding_.set_error_handler(fit::bind_member(this, &StreamImpl::Client::OnClientDisconnected));
  std::ostringstream oss;
  oss << "Camera Stream Client " << id;
  ZX_ASSERT(loop_.StartThread(oss.str().c_str()) == ZX_OK);
}

StreamImpl::Client::~Client() { loop_.Shutdown(); }

void StreamImpl::Client::PostSendFrame(fuchsia::camera3::FrameInfo frame) {
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), [this, frame = std::move(frame)]() mutable {
              frame_callback_(std::move(frame));
              frame_callback_ = nullptr;
            }) == ZX_OK);
}

void StreamImpl::Client::PostCloseConnection(zx_status_t epitaph) {
  zx_status_t status =
      async::PostTask(loop_.dispatcher(), [this, epitaph] { CloseConnection(epitaph); });
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

void StreamImpl::Client::PostReceiveBufferCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), [this, token_handle = std::move(token)]() mutable {
              fuchsia::sysmem::BufferCollectionTokenPtr token;
              zx_status_t status = token.Bind(std::move(token_handle));
              if (status != ZX_OK) {
                ZX_ASSERT(status == ZX_ERR_CANCELED);
                // Thread is shutting down.
                return;
              }
              token->Sync(
                  [this, token = std::move(token)]() mutable { buffers_.Set(std::move(token)); });
            }) == ZX_OK);
}

void StreamImpl::Client::PostReceiveResolution(fuchsia::math::Size coded_size) {
  zx_status_t status =
      async::PostTask(loop_.dispatcher(), [this, coded_size] { resolution_.Set(coded_size); });
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

void StreamImpl::Client::PostReceiveCropRegion(std::unique_ptr<fuchsia::math::RectF> region) {
  zx_status_t status =
      async::PostTask(loop_.dispatcher(), [this, region = std::move(region)]() mutable {
        // TODO(fxbug.dev/51176): Because unique_ptr is non-copyable, the hanging get helper assumes
        // all values are never the same as previous values. In this case, however, back-to-back
        // null regions or identical regions should not be sent twice.
        crop_region_.Set(std::move(region));
      });
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

bool& StreamImpl::Client::Participant() { return participant_; }

void StreamImpl::Client::OnClientDisconnected(zx_status_t status) {
  FX_PLOGS(DEBUG, status) << "Stream client " << id_ << " disconnected.";
  stream_.PostRemoveClient(id_);
}

void StreamImpl::Client::CloseConnection(zx_status_t status) {
  binding_.Close(status);
  stream_.PostRemoveClient(id_);
}

void StreamImpl::Client::GetProperties(GetPropertiesCallback callback) {
  fuchsia::camera3::StreamProperties properties;
  stream_.properties_.Clone(&properties);
  callback(std::move(properties));
}

void StreamImpl::Client::SetCropRegion(std::unique_ptr<fuchsia::math::RectF> region) {
  if (!stream_.properties_.supports_crop_region) {
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

  stream_.PostSetCropRegion(id_, std::move(region));
}

void StreamImpl::Client::WatchCropRegion(WatchCropRegionCallback callback) {
  if (crop_region_.Get(std::move(callback))) {
    CloseConnection(ZX_ERR_BAD_STATE);
  }
}

void StreamImpl::Client::SetResolution(fuchsia::math::Size coded_size) {
  stream_.PostSetResolution(id_, coded_size);
}

void StreamImpl::Client::WatchResolution(WatchResolutionCallback callback) {
  if (resolution_.Get(std::move(callback))) {
    CloseConnection(ZX_ERR_BAD_STATE);
  }
}

void StreamImpl::Client::SetBufferCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  stream_.PostSetBufferCollection(id_, std::move(token));
}

void StreamImpl::Client::WatchBufferCollection(WatchBufferCollectionCallback callback) {
  if (buffers_.Get(std::move(callback))) {
    CloseConnection(ZX_ERR_BAD_STATE);
  }
}

void StreamImpl::Client::WatchOrientation(WatchOrientationCallback callback) {
  // Orientation is not currently reported by hardware. Assume UP (no transform).
  callback(fuchsia::camera3::Orientation::UP);
}

void StreamImpl::Client::GetNextFrame(GetNextFrameCallback callback) {
  if (frame_callback_) {
    FX_LOGS(INFO) << "Client called GetNextFrame while a previous call was still pending.";
    CloseConnection(ZX_ERR_BAD_STATE);
    return;
  }

  frame_callback_ = std::move(callback);

  stream_.PostAddFrameSink(id_);
}

void StreamImpl::Client::Rebind(fidl::InterfaceRequest<Stream> request) {
  stream_.OnNewRequest(std::move(request));
}
