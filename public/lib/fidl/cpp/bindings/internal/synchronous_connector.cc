// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/synchronous_connector.h"

#include <mx/channel.h>
#include <utility>

#include "lib/fidl/cpp/bindings/message.h"
#include "lib/ftl/logging.h"

namespace fidl {
namespace internal {

SynchronousConnector::SynchronousConnector(mx::channel handle)
    : handle_(std::move(handle)) {}

SynchronousConnector::~SynchronousConnector() {}

bool SynchronousConnector::Write(Message* msg_to_send) {
  FTL_DCHECK(handle_);
  FTL_DCHECK(msg_to_send);

  mx_status_t rv = handle_.write(
      0, msg_to_send->data(), msg_to_send->data_num_bytes(),
      msg_to_send->mutable_handles()->empty()
          ? nullptr
          : reinterpret_cast<const mx_handle_t*>(
                msg_to_send->mutable_handles()->data()),
      static_cast<uint32_t>(msg_to_send->mutable_handles()->size()));

  switch (rv) {
    case NO_ERROR:
      break;

    default:
      FTL_LOG(WARNING) << "mx_channel_write unsuccessful. error = " << rv;
      return false;
  }

  return true;
}

bool SynchronousConnector::BlockingRead(Message* received_msg) {
  FTL_DCHECK(handle_);
  FTL_DCHECK(received_msg);

  mx_status_t rv =
      handle_.wait_one(MX_SIGNAL_READABLE, MX_TIME_INFINITE, nullptr);

  if (rv != NO_ERROR) {
    FTL_LOG(WARNING) << "Failed waiting for a response. error = " << rv;
    return false;
  }

  rv = ReadMessage(handle_, received_msg);
  if (rv != NO_ERROR) {
    FTL_LOG(WARNING) << "Failed reading the response message. error = " << rv;
    return false;
  }

  return true;
}

}  // namespace internal
}  // namespace fidl
