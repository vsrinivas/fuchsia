// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_MESSAGE_CONTAINER_MESSAGE_CHECKERS_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_MESSAGE_CONTAINER_MESSAGE_CHECKERS_H_

#include <lib/fidl/llcpp/message.h>

namespace fidl_testing {

// |MessageChecker| is a friend of |fidl::OutgoingMessage|, and lets us write
// tests that depend on the private states of message objects.
class MessageChecker {
 public:
  static fidl_outgoing_msg_t* GetCMessage(::fidl::OutgoingMessage& msg) { return &msg.message_; }
};

}  // namespace fidl_testing

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_MESSAGE_CONTAINER_MESSAGE_CHECKERS_H_
