// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_MESSAGE_SENDER_H_
#define LIB_FIDL_CPP_INTERNAL_MESSAGE_SENDER_H_

#include <lib/fidl/cpp/message.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

namespace fidl {
namespace internal {

// An interface for sending FIDL messages.
class MessageSender {
 public:
  virtual ~MessageSender();

  // Send a message over the channel.
  //
  // Returns an error if the message fails to encode properly or if the message
  // cannot be written to the channel.
  virtual zx_status_t Send(const fidl_type_t* type, HLCPPOutgoingMessage message) = 0;
};

// Send a message over the channel.
//
// Returns an error if the message fails to encode properly or if the message
// cannot be written to the channel.
zx_status_t SendMessage(const zx::channel& channel, const fidl_type_t* type,
                        HLCPPOutgoingMessage message);

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_MESSAGE_SENDER_H_
