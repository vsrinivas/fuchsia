// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>

#include "channel.h"
#include "session.h"

namespace btlib {
namespace rfcomm {

Channel::Channel(DLCI dlci, Session* session)
    : dlci_(dlci), session_(session){};

namespace internal {

void ChannelImpl::CallRxCallback(common::ByteBufferPtr data) {
  async::PostTask(dispatcher_, [this, data_ = std::move(data)]() mutable {
    rx_callback_(std::move(data_));
  });
}

void ChannelImpl::Activate(RxCallback rx_callback,
                           ClosedCallback closed_callback,
                           async_dispatcher_t* dispatcher) {
  rx_callback_ = std::move(rx_callback);
  closed_callback_ = std::move(closed_callback_);
  dispatcher_ = dispatcher;

  while (!pending_rxed_frames_.empty()) {
    auto& data = pending_rxed_frames_.front();
    CallRxCallback(std::move(data));
    pending_rxed_frames_.pop();
  }
}

void ChannelImpl::Send(common::ByteBufferPtr data) {
  FXL_DCHECK(session_);
  session_->Send(dlci_, std::move(data));
}

void ChannelImpl::Receive(std::unique_ptr<common::ByteBuffer> data) {
  if (rx_callback_) {
    CallRxCallback(std::move(data));
  } else {
    pending_rxed_frames_.push(std::move(data));
  }
}

}  // namespace internal
}  // namespace rfcomm
}  // namespace btlib
