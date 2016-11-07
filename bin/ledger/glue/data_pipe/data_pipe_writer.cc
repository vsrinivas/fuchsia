// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/data_pipe/data_pipe_writer.h"

#include <algorithm>
#include <utility>

#include <string.h>

#include "lib/ftl/logging.h"

namespace glue {

DataPipeWriter::DataPipeWriter(const FidlAsyncWaiter* waiter)
    : waiter_(waiter) {}

DataPipeWriter::~DataPipeWriter() {
  if (wait_id_) {
    waiter_->CancelWait(wait_id_);
  }
}

void DataPipeWriter::Start(const std::string& data,
                           mx::datapipe_producer destination) {
  data_ = data;
  destination_ = std::move(destination);
  WriteData();
}

void DataPipeWriter::WriteData() {
  mx_status_t rv_begin = destination_.begin_write(
      0u, reinterpret_cast<uintptr_t*>(&buffer_), &buffer_size_);
  if (rv_begin == NO_ERROR) {
    size_t num_bytes =
        std::min(static_cast<size_t>(buffer_size_), data_.size() - offset_);
    memcpy(buffer_, &data_[offset_], num_bytes);
    mx_status_t rv_end = destination_.end_write(num_bytes);
    FTL_DCHECK(rv_end == NO_ERROR);
    offset_ += num_bytes;
    if (offset_ < data_.size()) {
      WaitForPipe();
    } else {
      Done();
    }
  } else if (rv_begin == ERR_SHOULD_WAIT) {
    WaitForPipe();
  } else if (rv_begin == ERR_REMOTE_CLOSED) {
    Done();
  } else {
    FTL_DCHECK(false) << "Unhandled mx_status_t: " << rv_begin;
  }
}

void DataPipeWriter::WaitForPipe() {
  wait_id_ = waiter_->AsyncWait(destination_.get(),
                                MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED,
                                MX_TIME_INFINITE, &WaitComplete, this);
}

// static
void DataPipeWriter::WaitComplete(mx_status_t result,
                                  mx_signals_t pending,
                                  void* context) {
  DataPipeWriter* writer = static_cast<DataPipeWriter*>(context);
  writer->wait_id_ = 0;
  writer->WriteData();
}

void DataPipeWriter::Done() {
  delete this;
}

}  // namespace glue
