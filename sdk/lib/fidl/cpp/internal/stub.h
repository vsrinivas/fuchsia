// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_STUB_H_
#define LIB_FIDL_CPP_INTERNAL_STUB_H_

#include <lib/fidl/cpp/internal/pending_response.h>
#include <lib/fidl/cpp/message.h>
#include <zircon/types.h>

namespace fidl {
namespace internal {
class MessageSender;

// An interface for dispatching FIDL messages to a protocol implementation.
//
// Used by |StubController| to supply both a |Message| and a |PendingResponse|
// object to protocol implementations.
class Stub {
 public:
  virtual ~Stub();

  // A new message has arrived.
  //
  // If the message expects a response, the |PendingResponse| object's
  // |needs_response()| method will return true.
  //
  // The memory backing the message will remain valid until this method returns,
  // at which point the memory might or might not be deallocated.
  //
  // The |PendingResponse| object has affinity for the current thread and is not
  // safe to transport to another thread.
  virtual zx_status_t Dispatch_(HLCPPIncomingMessage message, PendingResponse response) = 0;

  // The protocol-agnostic object that can send messages.
  //
  // The sender must be set to a non-null value before sending events
  // through this stub.
  MessageSender* sender_() const { return message_sender_; }
  void set_sender(MessageSender* sender) { message_sender_ = sender; }

 private:
  MessageSender* message_sender_ = nullptr;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_STUB_H_
