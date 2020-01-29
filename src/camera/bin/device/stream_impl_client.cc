// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/stream_impl.h"

#include <lib/async-loop/default.h>
#include <sstream>
#include <lib/syslog/cpp/logger.h>

StreamImpl::Client::Client(StreamImpl& stream)
    : stream_(stream), loop_(&kAsyncLoopConfigNoAttachToCurrentThread), binding_(this) {}

StreamImpl::Client::~Client() { loop_.Shutdown(); }

fit::result<std::unique_ptr<StreamImpl::Client>, zx_status_t> StreamImpl::Client::Create(
    StreamImpl& device, uint64_t id, fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  FX_LOGS(DEBUG) << "Stream client " << id << " connected.";

  auto client = std::make_unique<Client>(device);

  std::ostringstream oss;
  oss << "Camera Stream Thread (Client ID = " << id << ")";
  zx_status_t status = client->loop_.StartThread(oss.str().c_str());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    request.Close(ZX_ERR_INTERNAL);
    return fit::error(status);
  }

  status = client->binding_.Bind(std::move(request), client->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  client->id_ = id;

  return fit::ok(std::move(client));
}

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
