// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/glue/data_pipe/data_pipe_writer.h"

#include <algorithm>
#include <utility>

#include <string.h>

#include "lib/ftl/logging.h"

namespace glue {

DataPipeWriter::DataPipeWriter(const MojoAsyncWaiter* waiter)
    : waiter_(waiter) {}

DataPipeWriter::~DataPipeWriter() {
  if (wait_id_) {
    waiter_->CancelWait(wait_id_);
  }
}

void DataPipeWriter::Start(const std::string& data,
                           mojo::ScopedDataPipeProducerHandle destination) {
  data_ = data;
  destination_ = std::move(destination);
  WriteData();
}

void DataPipeWriter::WriteData() {
  MojoResult rv_begin = mojo::BeginWriteDataRaw(
      destination_.get(), &buffer_, &buffer_size_, MOJO_READ_DATA_FLAG_NONE);
  if (rv_begin == MOJO_RESULT_OK) {
    size_t num_bytes =
        std::min(static_cast<size_t>(buffer_size_), data_.size() - offset_);
    memcpy(buffer_, &data_[offset_], num_bytes);
    MojoResult rv_end = mojo::EndWriteDataRaw(destination_.get(), num_bytes);
    FTL_DCHECK(rv_end == MOJO_RESULT_OK);
    offset_ += num_bytes;
    if (offset_ < data_.size()) {
      WaitForPipe();
    } else {
      Done();
    }
  } else if (rv_begin == MOJO_RESULT_SHOULD_WAIT) {
    WaitForPipe();
  } else if (rv_begin == MOJO_RESULT_FAILED_PRECONDITION) {
    Done();
  } else {
    FTL_DCHECK(false) << "Unhandled MojoResult: " << rv_begin;
  }
}

void DataPipeWriter::WaitForPipe() {
  wait_id_ = waiter_->AsyncWait(destination_.get().value(),
                                MOJO_HANDLE_SIGNAL_WRITABLE,
                                MOJO_DEADLINE_INDEFINITE, &WaitComplete, this);
}

// static
void DataPipeWriter::WaitComplete(void* context, MojoResult result) {
  DataPipeWriter* writer = static_cast<DataPipeWriter*>(context);
  writer->wait_id_ = 0;
  writer->WriteData();
}

void DataPipeWriter::Done() {
  delete this;
}

}  // namespace glue
