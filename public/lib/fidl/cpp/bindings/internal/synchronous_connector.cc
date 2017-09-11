// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/synchronous_connector.h"

#include <mx/channel.h>
#include <utility>

#include "lib/fidl/cpp/bindings/message.h"
#include "lib/fxl/logging.h"

namespace fidl {
namespace internal {

SynchronousConnector::SynchronousConnector(mx::channel handle)
    : handle_(std::move(handle)) {}

SynchronousConnector::~SynchronousConnector() {}

bool SynchronousConnector::Write(Message* msg_to_send) {
  FXL_DCHECK(handle_);
  FXL_DCHECK(msg_to_send);

  mx_status_t rv = handle_.write(
      0, msg_to_send->data(), msg_to_send->data_num_bytes(),
      msg_to_send->mutable_handles()->empty()
          ? nullptr
          : reinterpret_cast<const mx_handle_t*>(
                msg_to_send->mutable_handles()->data()),
      static_cast<uint32_t>(msg_to_send->mutable_handles()->size()));

  return rv == MX_OK;
}

bool SynchronousConnector::BlockingRead(Message* received_msg) {
  FXL_DCHECK(handle_);
  FXL_DCHECK(received_msg);

  mx_signals_t pending;
  mx_status_t rv = handle_.wait_one(MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                                    MX_TIME_INFINITE, &pending);

  if (rv != MX_OK) {
    return false;
  }

  if (pending & MX_CHANNEL_READABLE) {
    rv = ReadMessage(handle_, received_msg);
    if (rv != MX_OK) {
      return false;
    }
    return true;
  } else if (pending & MX_CHANNEL_PEER_CLOSED) {
    // There aren't any more messages to read out of the channel and the peer is
    // closed.
    return false;
  }

  FXL_NOTREACHED()
      << "Failed to receive one of the expected signals. pending = " << pending;
  return false;
}

}  // namespace internal
}  // namespace fidl
