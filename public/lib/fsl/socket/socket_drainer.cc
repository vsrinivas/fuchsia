// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/socket/socket_drainer.h"

#include <zx/socket.h>
#include <utility>
#include <vector>

#include "lib/fxl/logging.h"

namespace fsl {

SocketDrainer::Client::~Client() = default;

SocketDrainer::SocketDrainer(Client* client, async_t* async)
    : client_(client),
      async_(async),
      destruction_sentinel_(nullptr) {
  FXL_DCHECK(client_);
}

SocketDrainer::~SocketDrainer() {
  wait_.Cancel(async_);
  if (destruction_sentinel_)
    *destruction_sentinel_ = true;
}

void SocketDrainer::Start(zx::socket source) {
  source_ = std::move(source);
  async_wait_result_t result = OnHandleReady(async_, ZX_OK, nullptr);
  if (result == ASYNC_WAIT_AGAIN) {
    wait_.set_object(source_.get());
    wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED
                      | ZX_SOCKET_PEER_CLOSED);
    wait_.set_handler(fbl::BindMember(this, &SocketDrainer::OnHandleReady));
    wait_.Begin(async_);
  }
}

async_wait_result_t SocketDrainer::OnHandleReady(
    async_t* async, zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    client_->OnDataComplete();
    return ASYNC_WAIT_FINISHED;
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
      return ASYNC_WAIT_FINISHED;
    destruction_sentinel_ = nullptr;
    return ASYNC_WAIT_AGAIN;
  }

  if (status == ZX_ERR_SHOULD_WAIT)
    return ASYNC_WAIT_AGAIN;

  FXL_DCHECK(status == ZX_ERR_PEER_CLOSED || status == ZX_ERR_BAD_STATE)
      << "Unhandled zx_status_t: " << status;
  client_->OnDataComplete();
  return ASYNC_WAIT_FINISHED;
}

}  // namespace fsl
