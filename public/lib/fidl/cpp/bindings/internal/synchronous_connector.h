// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_SYNCHRONOUS_CONNECTOR_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_SYNCHRONOUS_CONNECTOR_H_

#include <mx/channel.h>

#include "lib/fidl/cpp/bindings/message.h"
#include "lib/fxl/macros.h"

namespace fidl {
namespace internal {

// This class is responsible for performing synchronous read/write operations on
// a channel. Notably, this interface allows us to make synchronous
// request/response operations on messages: write a message (that expects a
// response message), wait on the channel, and read the response.
class SynchronousConnector {
 public:
  explicit SynchronousConnector(mx::channel handle);
  ~SynchronousConnector();

  // This will mutate the message by moving the handles out of it. |msg_to_send|
  // must be non-null. Returns true on a successful write.
  bool Write(Message* msg_to_send);

  // This method blocks indefinitely until a message is received. |received_msg|
  // must be non-null and be empty. Returns true on a successful read.
  // TODO(vardhan): Add a timeout mechanism.
  bool BlockingRead(Message* received_msg);

  mx::channel PassHandle() { return std::move(handle_); }

  // Returns true if the underlying channel is valid.
  bool is_valid() const { return !!handle_; }

 private:
  mx::channel handle_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SynchronousConnector);
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_SYNCHRONOUS_CONNECTOR_H_
