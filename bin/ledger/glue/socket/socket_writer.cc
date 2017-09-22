// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/glue/socket/socket_writer.h"

#include <string.h>

#include <algorithm>
#include <utility>

#include "lib/fxl/logging.h"

namespace glue {

// TODO(qsr): Remove this, and retrieve the buffer size from the socket when
// available.
constexpr size_t kDefaultSocketBufferSize = 256 * 1024u;

SocketWriter::SocketWriter(Client* client, const FidlAsyncWaiter* waiter)
    : client_(client), waiter_(waiter) {}

SocketWriter::~SocketWriter() {
  if (wait_id_) {
    waiter_->CancelWait(wait_id_);
  }
}

void SocketWriter::Start(zx::socket destination) {
  destination_ = std::move(destination);
  GetData();
}

void SocketWriter::GetData() {
  FXL_DCHECK(data_.empty());
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
    WaitForSocket();
    return;
  }
  FXL_DCHECK(false) << "Unhandled zx_status_t: " << status;
}

void SocketWriter::WaitForSocket() {
  wait_id_ = waiter_->AsyncWait(destination_.get(),
                                ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED,
                                ZX_TIME_INFINITE, &WaitComplete, this);
}

// static
void SocketWriter::WaitComplete(zx_status_t /*result*/,
                                zx_signals_t /*pending*/,
                                uint64_t /*count*/,
                                void* context) {
  SocketWriter* writer = static_cast<SocketWriter*>(context);
  writer->wait_id_ = 0;
  writer->WriteData(writer->data_view_);
}

void SocketWriter::Done() {
  destination_.reset();
  client_->OnDataComplete();
}

StringSocketWriter::StringSocketWriter(const FidlAsyncWaiter* waiter)
    : socket_writer_(this, waiter) {}

void StringSocketWriter::Start(std::string data, zx::socket destination) {
  data_ = std::move(data);
  socket_writer_.Start(std::move(destination));
}

void StringSocketWriter::GetNext(
    size_t offset,
    size_t max_size,
    std::function<void(fxl::StringView)> callback) {
  fxl::StringView data = data_;
  callback(data.substr(offset, max_size));
}

void StringSocketWriter::OnDataComplete() {
  delete this;
}

}  // namespace glue
