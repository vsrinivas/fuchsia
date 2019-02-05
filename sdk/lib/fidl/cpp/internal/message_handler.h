// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_MESSAGE_HANDLER_H_
#define LIB_FIDL_CPP_INTERNAL_MESSAGE_HANDLER_H_

#include <lib/fidl/cpp/message.h>
#include <zircon/types.h>

namespace fidl {
namespace internal {

// An interface for receiving FIDL messages.
//
// Used by |MessageReader| to call back into its client whenever it reads a
// message from the channel.
class MessageHandler {
 public:
  virtual ~MessageHandler();

  // A new message has arrived.
  //
  // The memory backing the message will remain valid until this method returns,
  // at which point the memory might or might not be deallocated.
  virtual zx_status_t OnMessage(Message message) = 0;

  // The channel from which the messages were being read is gone.
  //
  // The channel's peer might have been closed or the |MessageReader| might have
  // unbound from the channel. In either case, implementations that keep
  // per-channel state should reset their state when this method is called.
  virtual void OnChannelGone();
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_MESSAGE_HANDLER_H_
