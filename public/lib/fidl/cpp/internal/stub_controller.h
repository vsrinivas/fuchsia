// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_STUB_CONTROLLER_H_
#define LIB_FIDL_CPP_INTERNAL_STUB_CONTROLLER_H_

#include <memory>

#include <lib/fidl/cpp/message.h>
#include <lib/zx/channel.h>

#include "lib/fidl/cpp/internal/message_handler.h"
#include "lib/fidl/cpp/internal/message_reader.h"
#include "lib/fidl/cpp/internal/stub.h"

namespace fidl {
namespace internal {
class WeakStubController;

// Controls the server endpoint of a FIDL channel.
//
// A |StubController| controls the protocol-specific "stub" object. Stub
// objects are used on the server endpoint of a FIDL channel to decode messages
// received over the channel and dispatch them to an implementation of the
// protocol.
class StubController : public MessageHandler {
 public:
  StubController();
  ~StubController();

  StubController(const StubController&) = delete;
  StubController& operator=(const StubController&) = delete;

  // The |MessageReader| that is listening for messages sent by the client.
  MessageReader& reader() { return reader_; }
  const MessageReader& reader() const { return reader_; }

  // The protocol-specific object that decodes messages and dispatches them to
  // an implementation of the protocol.
  //
  // The stub must be set to a non-null value before messages are read from the
  // underlying channel. Typically, the caller will set a non-null stub before
  // binding a channel to the |MessageReader|.
  Stub* stub() const { return stub_; }
  void set_stub(Stub* stub) { stub_ = stub; }

  // Send a message over the channel.
  //
  // Returns an error if the message fails to encode properly or if the message
  // cannot be written to the channel.
  zx_status_t Send(const fidl_type_t* type, Message message);

 private:
  // Called by the |MessageReader| when a message arrives on the channel from
  // the client.
  //
  // The message will be dispatched using the |stub()|. If the message expects a
  // response, the |stub()| will also be given a |PendingResponse| object that
  // can be used to send a reply to the message.
  zx_status_t OnMessage(Message message) final;

  // Causes the |StubController| to invalidate all outstanding weak pointers,
  // preventing outstanding |PendingResponse| objects from sending messages on
  // the channel that has gone away.
  void OnChannelGone() final;

  // Invalidate all outstanding weak pointers, preventing outstanding
  // |PendingResponse| objects from sending messages.
  void InvalidateWeakIfNeeded();

  WeakStubController* weak_;
  MessageReader reader_;
  Stub* stub_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_STUB_CONTROLLER_H_
