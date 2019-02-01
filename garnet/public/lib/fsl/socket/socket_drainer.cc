// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/socket/socket_drainer.h"

#include <lib/zx/socket.h>
#include <utility>
#include <vector>

#include "lib/fxl/logging.h"

namespace fsl {

SocketDrainer::Client::~Client() = default;

SocketDrainer::SocketDrainer(Client* client, async_dispatcher_t* dispatcher)
    : client_(client), dispatcher_(dispatcher), destruction_sentinel_(nullptr) {
  FXL_DCHECK(client_);
}

SocketDrainer::~SocketDrainer() {
  wait_.Cancel();
  if (destruction_sentinel_)
    *destruction_sentinel_ = true;
}

void SocketDrainer::Start(zx::socket source) {
  source_ = std::move(source);
  wait_.set_object(source_.get());
  wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED |
                    ZX_SOCKET_PEER_CLOSED);
  OnHandleReady(dispatcher_, &wait_, ZX_OK, nullptr);
}

void SocketDrainer::OnHandleReady(async_dispatcher_t* dispatcher,
                                  async::WaitBase* wait, zx_status_t status,
                                  const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    client_->OnDataComplete();
    return;
  }

  std::vector<char> buffer(64 * 1024);
  size_t num_bytes = 0;
  status = source_.read(0, buffer.data(), buffer.size(), &num_bytes);
  if (status == ZX_OK) {
    // Calling the user callback, and exiting early if this objects is
    // destroyed.
    bool is_destroyed = false;
    destruction_sentinel_ = &is_destroyed;
    client_->OnDataAvailable(buffer.data(), num_bytes);
    if (is_destroyed)
      return;
    destruction_sentinel_ = nullptr;
    status = ZX_ERR_SHOULD_WAIT;
  }

  if (status == ZX_ERR_SHOULD_WAIT) {
    status = wait->Begin(dispatcher);
    if (status != ZX_OK) {
      client_->OnDataComplete();
    }
    return;
  }

  FXL_DCHECK(status == ZX_ERR_PEER_CLOSED || status == ZX_ERR_BAD_STATE)
      << "Unhandled zx_status_t: " << status;
  client_->OnDataComplete();
}

}  // namespace fsl
