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
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::Client::PostSendFrame");
  TRACE_FLOW_BEGIN("camera", "post_send_frame", nonce);
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), [this, frame = std::move(frame), nonce]() mutable {
              TRACE_DURATION("camera", "StreamImpl::Client::PostSendFrame.task");
              TRACE_FLOW_END("camera", "post_send_frame", nonce);
              // This Flow can be connected on the client end to allow tracing the flow of frames
              // into the client.
              TRACE_FLOW_BEGIN("camera", "camera3::Stream::GetNextFrame",
                               fsl::GetKoid(frame.release_fence.get()));
              frame_callback_(std::move(frame));
              frame_callback_ = nullptr;
            }) == ZX_OK);
}

void StreamImpl::Client::PostCloseConnection(zx_status_t epitaph) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::Client::PostCloseConnection");
  TRACE_FLOW_BEGIN("camera", "post_close_connection", nonce);
  zx_status_t status = async::PostTask(loop_.dispatcher(), [this, epitaph, nonce] {
    TRACE_DURATION("camera", "StreamImpl::Client::PostCloseConnection.task");
    TRACE_FLOW_END("camera", "post_close_connection", nonce);
    CloseConnection(epitaph);
  });
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

void StreamImpl::Client::PostReceiveBufferCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::Client::PostReceiveBufferCollection");
  TRACE_FLOW_BEGIN("camera", "post_receive_buffer_collection", nonce);
  ZX_ASSERT(
      async::PostTask(loop_.dispatcher(), [this, token_handle = std::move(token), nonce]() mutable {
        TRACE_DURATION("camera", "StreamImpl::Client::PostReceiveBufferCollection.task");
        TRACE_FLOW_END("camera", "post_receive_buffer_collection", nonce);
        fuchsia::sysmem::BufferCollectionTokenPtr token;
        zx_status_t status = token.Bind(std::move(token_handle));
        if (status != ZX_OK) {
          ZX_ASSERT(status == ZX_ERR_CANCELED);
          // Thread is shutting down.
          return;
        }
        token->Sync([this, token = std::move(token)]() mutable { buffers_.Set(std::move(token)); });
      }) == ZX_OK);
}

void StreamImpl::Client::PostReceiveResolution(fuchsia::math::Size coded_size) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::Client::PostReceiveResolution");
  TRACE_FLOW_BEGIN("camera", "post_receive_resolution", nonce);
  zx_status_t status = async::PostTask(loop_.dispatcher(), [this, coded_size, nonce] {
    TRACE_DURATION("camera", "StreamImpl::Client::PostReceiveResolution.task");
    TRACE_FLOW_END("camera", "post_receive_resolution", nonce);
    resolution_.Set(coded_size);
  });
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

void StreamImpl::Client::PostReceiveCropRegion(std::unique_ptr<fuchsia::math::RectF> region) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::Client::PostReceiveCropRegion");
  TRACE_FLOW_BEGIN("camera", "post_receive_crop_region", nonce);
  zx_status_t status =
      async::PostTask(loop_.dispatcher(), [this, region = std::move(region), nonce]() mutable {
        TRACE_DURATION("camera", "StreamImpl::Client::PostReceiveCropRegion.task");
        TRACE_FLOW_END("camera", "post_receive_crop_region", nonce);
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

  stream_.PostSetCropRegion(id_, std::move(region));
}

void StreamImpl::Client::WatchCropRegion(WatchCropRegionCallback callback) {
  TRACE_DURATION("camera", "StreamImpl::Client::WatchCropRegion");
  if (crop_region_.Get(std::move(callback))) {
    CloseConnection(ZX_ERR_BAD_STATE);
  }
}

void StreamImpl::Client::SetResolution(fuchsia::math::Size coded_size) {
  TRACE_DURATION("camera", "StreamImpl::Client::SetResolution");
  stream_.PostSetResolution(id_, coded_size);
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
  stream_.PostSetBufferCollection(id_, std::move(token));
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
  TRACE_DURATION("camera", "StreamImpl::Client::GetNextFrame");
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
