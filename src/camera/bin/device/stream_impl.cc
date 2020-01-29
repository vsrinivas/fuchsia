// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/stream_impl.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/camera/bin/device/messages.h"
#include "src/camera/bin/device/util.h"

StreamImpl::StreamImpl() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

StreamImpl::~StreamImpl() { loop_.Shutdown(); }

zx_status_t StreamImpl::Bind(fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  if (!clients_.empty()) {
    FX_PLOGS(INFO, ZX_ERR_ALREADY_BOUND) << Messages::kStreamAlreadyBound;
    request.Close(ZX_ERR_ALREADY_BOUND);
    return ZX_ERR_ALREADY_BOUND;
  }

  auto result = Client::Create(*this, client_id_next_, std::move(request));
  if (result.is_error()) {
    FX_PLOGS(ERROR, result.error());
    return result.error();
  }

  clients_[client_id_next_] = result.take_value();

  ++client_id_next_;

  return ZX_OK;
}

fit::result<std::unique_ptr<StreamImpl>, zx_status_t> StreamImpl::Create(
    fidl::InterfaceHandle<fuchsia::camera2::Stream> legacy_stream) {
  auto stream = std::make_unique<StreamImpl>();

  // Bind the stream interface and get some initial startup information.

  zx_status_t status =
      stream->legacy_stream_.Bind(std::move(legacy_stream), stream->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  // Start the stream thread and begin processing messages.

  status = stream->loop_.StartThread("Camera Stream Thread");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  // Rebind the legacy stream error handler.

  status = async::PostTask(stream->loop_.dispatcher(), [stream = stream.get()]() {
    stream->legacy_stream_.set_error_handler(
        fit::bind_member(stream, &StreamImpl::OnLegacyStreamDisconnected));
  });
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  return fit::ok(std::move(stream));
}

void StreamImpl::OnLegacyStreamDisconnected(zx_status_t status) {
  FX_PLOGS(ERROR, status) << "Legacy Stream disconnected unexpectedly.";
  clients_.clear();
}

void StreamImpl::PostRemoveClient(uint64_t id) {
  async::PostTask(loop_.dispatcher(), [=]() {
    auto it = clients_.find(id);
    if (it != clients_.end()) {
      clients_.erase(it);
    }
  });
}
