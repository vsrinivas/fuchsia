// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MESSAGE_QUEUE_CPP_MESSAGE_QUEUE_CLIENT_H_
#define LIB_MESSAGE_QUEUE_CPP_MESSAGE_QUEUE_CLIENT_H_

#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include "lib/fxl/macros.h"

namespace modular {

// MessageQueueClient is a wrapper class for using fuchsia.modular.MessageQueue
// in a more convinient way. This class represents messages as a std::string,
// which the underlying fidl interface may not.
//
// Usage:
//
// // 1. Obtain message queue and use it with MessageQueueClient.
// MessageQueueClient message_queue;
// component_context->ObtainMessageQueue("my_msg_q",
//                                       message_queue.NewRequest());
//
// // 2. Register receiver. New messages are sent to the supplied callback until
// //    the MessageQueueClient goes out of scope, or the receiver is
// //    unregistered. To unregister the receiver, call this method with a
// //    |nullptr| receiver.
// message_queue.RegisterReceiver([] (std::string msg, fit::function<void> ack){
//       ack();  // Acknowledge message receipt. We will continue to have new
//               // messages delivered to this callback.
//       FXL_LOG(INFO) << "new message: " << msg;
//     });
class MessageQueueClient : public fuchsia::modular::MessageReader {
 public:
  using ReceiverCallback =
      fit::function<void(std::string message, fit::function<void()> ack)>;

  MessageQueueClient();

  // Creates a new interface pair, binds one end to this object and returns the
  // request side. The previous message queue and receiver are unbound.
  fidl::InterfaceRequest<fuchsia::modular::MessageQueue> NewRequest();

  // Register a receiver callback, which will be called everytime there is a new
  // message. The receiver is supplied the message and an acknowledgement
  // callback which the receiver must call, to acknowledge that the message has
  // been processed and does not need to be received again. Supplying a
  // |nullptr| will unregister the previous receiver.
  void RegisterReceiver(ReceiverCallback receiver);

  // Returns a token for this message queue, which is used to send messages
  // to this message queue.
  void GetToken(std::function<void(fidl::StringPtr)> callback);

 private:
  // |fuchsia::modular::MessageReader|
  void OnReceive(fuchsia::mem::Buffer message,
                 std::function<void()> ack) override;

  fuchsia::modular::MessageQueuePtr queue_;
  fidl::Binding<fuchsia::modular::MessageReader> reader_;
  ReceiverCallback receiver_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageQueueClient);
};

}  // namespace modular

#endif  // LIB_MESSAGE_QUEUE_CPP_MESSAGE_QUEUE_CLIENT_H_
