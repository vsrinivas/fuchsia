// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/logger.h>

#include <sstream>

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

void StreamImpl::Client::SetResolution(uint32_t index) { CloseConnection(ZX_ERR_NOT_SUPPORTED); }

void StreamImpl::Client::WatchResolution(WatchResolutionCallback callback) {
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void StreamImpl::Client::GetNextFrame(GetNextFrameCallback callback) {
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void StreamImpl::Client::Rebind(fidl::InterfaceRequest<Stream> request) {
  request.Close(ZX_ERR_NOT_SUPPORTED);
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}
