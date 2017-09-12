// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/socket/socket_drainer.h"

#include <mx/socket.h>
#include <utility>
#include <vector>

#include "lib/fxl/logging.h"

namespace fsl {

SocketDrainer::Client::~Client() = default;

SocketDrainer::SocketDrainer(Client* client, const FidlAsyncWaiter* waiter)
    : client_(client),
      waiter_(waiter),
      wait_id_(0),
      destruction_sentinel_(nullptr) {
  FXL_DCHECK(client_);
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
  std::vector<char> buffer(64 * 1024);
  size_t num_bytes = 0;
  mx_status_t rv = source_.read(0, buffer.data(), buffer.size(), &num_bytes);
  if (rv == MX_OK) {
    // Calling the user callback, and exiting early if this objects is
    // destroyed.
    bool is_destroyed = false;
    destruction_sentinel_ = &is_destroyed;
    client_->OnDataAvailable(buffer.data(), num_bytes);
    if (is_destroyed)
      return;
    destruction_sentinel_ = nullptr;

    WaitForData();
  } else if (rv == MX_ERR_SHOULD_WAIT) {
    WaitForData();
  } else if (rv == MX_ERR_PEER_CLOSED || rv == MX_ERR_BAD_STATE) {
    client_->OnDataComplete();
  } else {
    FXL_DCHECK(false) << "Unhandled mx_status_t: " << rv;
  }
}

void SocketDrainer::WaitForData() {
  FXL_DCHECK(!wait_id_);
  wait_id_ = waiter_->AsyncWait(
      source_.get(),
      MX_SOCKET_READABLE | MX_SOCKET_READ_DISABLED | MX_SOCKET_PEER_CLOSED,
      MX_TIME_INFINITE, &WaitComplete, this);
}

void SocketDrainer::WaitComplete(mx_status_t result,
                                 mx_signals_t pending,
                                 uint64_t count,
                                 void* context) {
  SocketDrainer* drainer = static_cast<SocketDrainer*>(context);
  drainer->wait_id_ = 0;
  drainer->ReadData();
}

}  // namespace fsl
