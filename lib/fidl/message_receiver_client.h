// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_MESSAGE_RECEIVER_CLIENT_H_
#define PERIDOT_LIB_FIDL_MESSAGE_RECEIVER_CLIENT_H_

#include <functional>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/string.h>

namespace modular {

// MessageReceiverAdapator implements a |modular.MessageReader| fidl interface
// for reading messages from a message queue. It provides a simple way to
// receive messages.
//
// Usage:
//
// // 1. Obtain message queue.
// MessageQueuePtr message_queue = ...
// component_context->ObtainMessageQueue("my_msg_q",
//                                       message_queue.NewRequest());
//
// // 2. Register receiver. New messages are sent to the supplied callback. When
// //    |receiver| goes out of scope, messages will no longer be delivered to
// //    the callback.
// auto receiver = make_unique<MessageReceiverAdapator>(message_queue.get(),
//     [] (fidl::StringPtr msg, std::function<void> ack){
//       ack();  // Acknowledge message receipt. We will continue to have new
//               // messages delivered to this callback.
//               // messages to this callback.
//       FXL_LOG(INFO) << "new message: " << msg;
//     });
//
class MessageReceiverClient : fuchsia::modular::MessageReader {
 public:
  using MessageReceiverClientCallback =
      std::function<void(fidl::StringPtr, OnReceiveCallback ack)>;

  explicit MessageReceiverClient(fuchsia::modular::MessageQueue* mq,
                                 MessageReceiverClientCallback callback);

  ~MessageReceiverClient() override;

 private:
  // |MessageReader|
  void OnReceive(fidl::StringPtr message, OnReceiveCallback ack) override;

  MessageReceiverClientCallback callback_;
  fidl::Binding<fuchsia::modular::MessageReader> receiver_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_FIDL_MESSAGE_RECEIVER_CLIENT_H_
