// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS2_INTERNAL_PROXY_CONTROLLER_H_
#define LIB_FIDL_CPP_BINDINGS2_INTERNAL_PROXY_CONTROLLER_H_

#include <fidl/cpp/message.h>
#include <fidl/cpp/message_builder.h>

#include <map>
#include <memory>

#include "lib/fidl/cpp/bindings2/internal/channel_reader.h"
#include "lib/fidl/cpp/bindings2/internal/message_handler.h"

namespace fidl {
namespace internal {

// Controls the client endpoint of a FIDL channel.
//
// A |ProxyController| controls the protocol-specific "proxy" object. Proxy
// objects are used on the client endpoint of a FIDL channel to encode messages
// into the channel and send them to the server endpoint, whose "stub" object
// decodes them and dispatches them to an implementation of the protocol.
class ProxyController : public MessageHandler {
 public:
  ProxyController();
  ~ProxyController();

  ProxyController(const ProxyController&) = delete;
  ProxyController& operator=(const ProxyController&) = delete;

  ProxyController(ProxyController&&);
  ProxyController& operator=(ProxyController&&);

  // The |ChannelReader| that is listening for responses to messages sent by
  // this object.
  ChannelReader& reader() { return reader_; }
  const ChannelReader& reader() const { return reader_; }

  // Send a message over the channel.
  //
  // If |response_handler| is non-null, the message will be assigned a
  // transaction identifier before being encoded and sent over the channel. The
  // |response_handler| will be retained by the |ProxyController| until the
  // |ProxyController| recieves a response to the message, at which time the
  // |ProxyController| will call the |OnMessage| method of the
  // |response_handler|.
  //
  // Returns an error if the message fails to encode properly or if the message
  // cannot be written to the channel.
  zx_status_t Send(MessageBuilder* builder,
                   std::unique_ptr<MessageHandler> response_handler);

  // Clears all the state associated with this |ProxyController|.
  //
  // After this method returns, the |ProxyController| is in the same state it
  // would have been in if freshly constructed.
  void Reset();

 private:
  // Called by the |ChannelReader| when a message arrives on the channel from
  // the server.
  //
  // The message might be a response to a previously sent message or an
  // unsolicited event.
  zx_status_t OnMessage(Message message) final;

  // Causes the |ProxyController| to |ClearPendingHandlers()|.
  void OnChannelGone() final;

  // Causes the |ProxyController| to destroy all pending response handlers and
  // reset its transition identifiers.
  void ClearPendingHandlers();

  ChannelReader reader_;
  std::map<zx_txid_t, std::unique_ptr<MessageHandler>> handlers_;
  zx_txid_t next_txid_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS2_INTERNAL_PROXY_CONTROLLER_H_
