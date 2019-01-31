// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MESSAGE_QUEUE_CPP_MESSAGE_SENDER_CLIENT_H_
#define LIB_MESSAGE_QUEUE_CPP_MESSAGE_SENDER_CLIENT_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

namespace modular {

// MessageSenderClient is a wrapper class for using
// fuchsia.modular.MessageSender in a more convinient way. This class represents
// messages as a std::string, which the underlying fidl interface may not.
//
// Usage:
//
// // 1. Given a message queue token (|msg_queue_token|), get a MessageSender
// //    out of it.
// MessageSenderClient message_sender;
// component_context->GetMessageSender(msg_queue_token,
//                                     message_sender.NewRequest());
//
// // 2. Send a message.
// message_sender.Send("hello");
class MessageSenderClient {
 public:
  MessageSenderClient();

  // A message sender must be bound (by calling NewRequest()) to this class
  // before Send() is called.
  void Send(fxl::StringView msg);

  // Binds a new interface connection and returns the request side.
  fidl::InterfaceRequest<fuchsia::modular::MessageSender> NewRequest();

  // Whether the underlying interface connection is currently bound.
  explicit operator bool() const { return sender_.is_bound(); }

 private:
  fuchsia::modular::MessageSenderPtr sender_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageSenderClient);
};

}  // namespace modular

#endif  // LIB_MESSAGE_QUEUE_CPP_MESSAGE_SENDER_CLIENT_H_
