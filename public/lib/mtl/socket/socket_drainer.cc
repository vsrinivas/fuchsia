// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/socket/socket_drainer.h"

#include <utility>

#include "lib/ftl/logging.h"
#include "mx/socket.h"

namespace mtl {

SocketDrainer::SocketDrainer(Client* client, const FidlAsyncWaiter* waiter)
    : client_(client),
      waiter_(waiter),
      wait_id_(0),
      destruction_sentinel_(nullptr) {
  FTL_DCHECK(client_);
}

SocketDrainer::~SocketDrainer() {
  if (wait_id_)
    waiter_->CancelWait(wait_id_);
  if (destruction_sentinel_)
    *destruction_sentinel_ = true;
}

void SocketDrainer::Start(mx::socket source) {
  source_ = std::move(source);
  ReadData();
}

void SocketDrainer::ReadData() {
  char buffer[64 * 1024];
  size_t num_bytes = 0;
  mx_status_t rv = source_.read(0, buffer, sizeof(buffer), &num_bytes);
  if (rv == NO_ERROR) {
    // Calling the user callback, and exiting early if this objects is
    // destroyed.
    bool is_destroyed = false;
    destruction_sentinel_ = &is_destroyed;
    client_->OnDataAvailable(buffer, num_bytes);
    if (is_destroyed)
      return;
    destruction_sentinel_ = nullptr;

    WaitForData();
  } else if (rv == ERR_SHOULD_WAIT) {
    WaitForData();
  } else if (rv == ERR_REMOTE_CLOSED) {
    client_->OnDataComplete();
  } else {
    FTL_DCHECK(false) << "Unhandled mx_status_t: " << rv;
  }
}

void SocketDrainer::WaitForData() {
  FTL_DCHECK(!wait_id_);
  wait_id_ = waiter_->AsyncWait(source_.get(),
                                MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                MX_TIME_INFINITE, &WaitComplete, this);
}

void SocketDrainer::WaitComplete(mx_status_t result,
                                 mx_signals_t pending,
                                 void* context) {
  SocketDrainer* drainer = static_cast<SocketDrainer*>(context);
  drainer->wait_id_ = 0;
  drainer->ReadData();
}

}  // namespace mtl
