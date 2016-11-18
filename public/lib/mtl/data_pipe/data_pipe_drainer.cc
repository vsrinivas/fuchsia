// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/data_pipe/data_pipe_drainer.h"

#include <utility>

#include "lib/ftl/logging.h"
#include "mx/datapipe.h"

namespace mtl {

DataPipeDrainer::DataPipeDrainer(Client* client, const FidlAsyncWaiter* waiter)
    : client_(client),
      waiter_(waiter),
      wait_id_(0),
      destruction_sentinel_(nullptr) {
  FTL_DCHECK(client_);
}

DataPipeDrainer::~DataPipeDrainer() {
  if (wait_id_)
    waiter_->CancelWait(wait_id_);
  if (destruction_sentinel_)
    *destruction_sentinel_ = true;
}

void DataPipeDrainer::Start(mx::datapipe_consumer source) {
  source_ = std::move(source);
  ReadData();
}

void DataPipeDrainer::ReadData() {
  void* buffer = 0u;
  mx_size_t num_bytes = 0;
  mx_status_t rv =
      source_.begin_read(0, reinterpret_cast<uintptr_t*>(&buffer), &num_bytes);
  if (rv == NO_ERROR) {
    // Calling the user callback, and exiting early if this objects is
    // destroyed.
    bool is_destroyed = false;
    destruction_sentinel_ = &is_destroyed;
    client_->OnDataAvailable(buffer, num_bytes);
    if (is_destroyed)
      return;
    destruction_sentinel_ = nullptr;

    source_.end_read(num_bytes);
    WaitForData();
  } else if (rv == ERR_SHOULD_WAIT) {
    WaitForData();
  } else if (rv == ERR_REMOTE_CLOSED) {
    client_->OnDataComplete();
  } else {
    FTL_DCHECK(false) << "Unhandled mx_status_t: " << rv;
  }
}

void DataPipeDrainer::WaitForData() {
  FTL_DCHECK(!wait_id_);
  wait_id_ = waiter_->AsyncWait(source_.get(),
                                MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                MX_TIME_INFINITE, &WaitComplete, this);
}

void DataPipeDrainer::WaitComplete(mx_status_t result,
                                   mx_signals_t pending,
                                   void* context) {
  DataPipeDrainer* drainer = static_cast<DataPipeDrainer*>(context);
  drainer->wait_id_ = 0;
  drainer->ReadData();
}

}  // namespace mtl
