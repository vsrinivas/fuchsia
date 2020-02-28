// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/logger.h>

#include <sstream>

#include "src/camera/bin/device/messages.h"
#include "src/camera/bin/device/stream_impl.h"

StreamImpl::Client::Client(StreamImpl& stream, uint64_t id,
                           fidl::InterfaceRequest<fuchsia::camera3::Stream> request)
    : stream_(stream),
      id_(id),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      binding_(this, std::move(request), loop_.dispatcher()) {
  FX_LOGS(DEBUG) << "Stream client " << id << " connected.";
  std::ostringstream oss;
  oss << "Camera Stream Thread (Client ID = " << id << ")";
  ZX_ASSERT(loop_.StartThread(oss.str().c_str()) == ZX_OK);
}

StreamImpl::Client::~Client() { loop_.Shutdown(); }

void StreamImpl::Client::PostSendFrame(fuchsia::camera3::FrameInfo frame) {
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), [this, frame = std::move(frame)]() mutable {
              frame_callback_(std::move(frame));
              frame_callback_ = nullptr;
            }) == ZX_OK);
}

void StreamImpl::Client::PostReceiveBufferCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), [this, token = std::move(token)]() mutable {
              if (watch_buffers_callback_) {
                ZX_ASSERT(!token_);
                watch_buffers_callback_(std::move(token));
                watch_buffers_callback_ = nullptr;
                return;
              }
              token_ = std::move(token);
            }) == ZX_OK);
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

void StreamImpl::Client::SetCropRegion(std::unique_ptr<fuchsia::math::RectF> region) {
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void StreamImpl::Client::WatchCropRegion(WatchCropRegionCallback callback) {
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void StreamImpl::Client::SetResolution(fuchsia::math::Size coded_size) {
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void StreamImpl::Client::WatchResolution(WatchResolutionCallback callback) {
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void StreamImpl::Client::SetBufferCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  stream_.PostSetBufferCollection(id_, std::move(token));
}

void StreamImpl::Client::WatchBufferCollection(WatchBufferCollectionCallback callback) {
  if (watch_buffers_callback_) {
    CloseConnection(ZX_ERR_BAD_STATE);
    return;
  }

  if (token_) {
    callback(std::move(token_));
    token_ = nullptr;
    return;
  }

  watch_buffers_callback_ = std::move(callback);
}

void StreamImpl::Client::GetNextFrame(GetNextFrameCallback callback) {
  if (stream_.max_camping_buffers_ == 0) {
    FX_LOGS(INFO) << Messages::kNoCampingBuffers;
  }

  if (frame_callback_) {
    FX_PLOGS(INFO, ZX_ERR_BAD_STATE)
        << "Client called GetNextFrame while a previous call was still pending.";
    CloseConnection(ZX_ERR_BAD_STATE);
    return;
  }

  frame_callback_ = std::move(callback);

  stream_.PostAddFrameSink(id_);
}

void StreamImpl::Client::Rebind(fidl::InterfaceRequest<Stream> request) {
  request.Close(ZX_ERR_NOT_SUPPORTED);
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}
