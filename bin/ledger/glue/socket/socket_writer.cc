// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/socket/socket_writer.h"

#include <string.h>

#include <algorithm>
#include <utility>

#include "lib/ftl/logging.h"

namespace glue {

SocketWriter::SocketWriter(const FidlAsyncWaiter* waiter) : waiter_(waiter) {}

SocketWriter::~SocketWriter() {
  if (wait_id_) {
    waiter_->CancelWait(wait_id_);
  }
}

void SocketWriter::Start(std::string data, mx::socket destination) {
  data_ = std::move(data);
  destination_ = std::move(destination);
  WriteData();
}

void SocketWriter::WriteData() {
  mx_status_t status = NO_ERROR;
  while (status == NO_ERROR && offset_ < data_.size()) {
    size_t written;
    status = destination_.write(0u, data_.data() + offset_,
                                data_.size() - offset_, &written);
    if (status == NO_ERROR)
      offset_ += written;
  }

  if (status == NO_ERROR || status == ERR_REMOTE_CLOSED) {
    Done();
    return;
  }
  if (status == ERR_SHOULD_WAIT) {
    WaitForSocket();
    return;
  }
  FTL_DCHECK(false) << "Unhandled mx_status_t: " << status;
}

void SocketWriter::WaitForSocket() {
  wait_id_ = waiter_->AsyncWait(destination_.get(),
                                MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED,
                                MX_TIME_INFINITE, &WaitComplete, this);
}

// static
void SocketWriter::WaitComplete(mx_status_t result,
                                mx_signals_t pending,
                                void* context) {
  SocketWriter* writer = static_cast<SocketWriter*>(context);
  writer->wait_id_ = 0;
  writer->WriteData();
}

void SocketWriter::Done() {
  delete this;
}

}  // namespace glue
