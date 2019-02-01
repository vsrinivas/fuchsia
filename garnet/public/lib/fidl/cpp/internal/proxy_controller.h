// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_PROXY_CONTROLLER_H_
#define LIB_FIDL_CPP_INTERNAL_PROXY_CONTROLLER_H_

#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_builder.h>

#include <map>
#include <memory>

#include "lib/fidl/cpp/internal/message_handler.h"
#include "lib/fidl/cpp/internal/message_reader.h"
#include "lib/fidl/cpp/internal/proxy.h"

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

  // The |MessageReader| that is listening for responses to messages sent by
  // this object.
  MessageReader& reader() { return reader_; }
  const MessageReader& reader() const { return reader_; }

  // The protocol-specific object that decodes messages and dispatches them to
  // an implementation of the protocol.
  //
  // The proxy must be set to a non-null value before messages are read from the
  // underlying channel. Typically, the caller will set a non-null proxy before
  // binding a channel to the |MessageReader|.
  Proxy* proxy() const { return proxy_; }
  void set_proxy(Proxy* proxy) { proxy_ = proxy; }

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
  zx_status_t Send(const fidl_type_t* type, Message message,
                   std::unique_ptr<MessageHandler> response_handler);

  // Clears all the state associated with this |ProxyController|.
  //
  // After this method returns, the |ProxyController| is in the same state it
  // would have been in if freshly constructed.
  void Reset();

 private:
  // Called by the |MessageReader| when a message arrives on the channel from
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

  MessageReader reader_;
  Proxy* proxy_ = nullptr;
  std::map<zx_txid_t, std::unique_ptr<MessageHandler>> handlers_;
  zx_txid_t next_txid_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_PROXY_CONTROLLER_H_
