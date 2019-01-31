// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/socket/socket_writer.h"

#include <string.h>
#include <algorithm>
#include <utility>

#include <lib/fit/function.h>
#include <lib/fxl/logging.h>

namespace socket {

// TODO(qsr): Remove this, and retrieve the buffer size from the socket when
// available.
constexpr size_t kDefaultSocketBufferSize = 256 * 1024u;

SocketWriter::SocketWriter(Client* client, async_dispatcher_t* dispatcher)
    : client_(client), dispatcher_(dispatcher) {
  wait_.set_trigger(ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED);
  wait_.set_handler(
      [this](async_dispatcher_t* dispatcher, async::Wait* wait,
             zx_status_t status,
             const zx_packet_signal_t* signal) { WriteData(data_view_); });
}

SocketWriter::~SocketWriter() = default;

void SocketWriter::Start(zx::socket destination) {
  destination_ = std::move(destination);
  wait_.Cancel();
  wait_.set_object(destination_.get());
  GetData();
}

void SocketWriter::GetData() {
  FXL_DCHECK(data_.empty());
  wait_.Cancel();
  client_->GetNext(offset_, kDefaultSocketBufferSize,
                   [this](fxl::StringView data) {
                     if (data.empty()) {
                       Done();
                       return;
                     }
                     offset_ += data.size();
                     WriteData(data);
                   });
}

void SocketWriter::WriteData(fxl::StringView data) {
  zx_status_t status = ZX_OK;
  while (status == ZX_OK && !data.empty()) {
    size_t written;
    status = destination_.write(0u, data.data(), data.size(), &written);
    if (status == ZX_OK) {
      data = data.substr(written);
    }
  }

  if (status == ZX_OK) {
    FXL_DCHECK(data.empty());
    data_.clear();
    data_view_ = "";
    GetData();
    return;
  }

  FXL_DCHECK(!data.empty());

  if (status == ZX_ERR_PEER_CLOSED) {
    Done();
    return;
  }

  if (status == ZX_ERR_SHOULD_WAIT) {
    if (data_.empty()) {
      data_ = data.ToString();
      data_view_ = data_;
    } else {
      data_view_ = data;
    }
    if (!wait_.is_pending())
      wait_.Begin(dispatcher_);
    return;
  }
  FXL_DCHECK(false) << "Unhandled zx_status_t: " << status;
}

void SocketWriter::Done() {
  wait_.Cancel();
  destination_.reset();
  client_->OnDataComplete();
}

StringSocketWriter::StringSocketWriter(async_dispatcher_t* dispatcher)
    : socket_writer_(this, dispatcher) {}

void StringSocketWriter::Start(std::string data, zx::socket destination) {
  data_ = std::move(data);
  socket_writer_.Start(std::move(destination));
}

void StringSocketWriter::GetNext(
    size_t offset, size_t max_size,
    fit::function<void(fxl::StringView)> callback) {
  fxl::StringView data = data_;
  callback(data.substr(offset, max_size));
}

void StringSocketWriter::OnDataComplete() { delete this; }

}  // namespace socket
