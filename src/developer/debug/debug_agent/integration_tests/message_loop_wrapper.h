// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_INTEGRATION_TESTS_MESSAGE_LOOP_WRAPPER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_INTEGRATION_TESTS_MESSAGE_LOOP_WRAPPER_H_

#include "src/developer/debug/shared/platform_message_loop.h"

namespace debug_agent {

// Inits a message loop on construction and clears it on destruction.
class MessageLoopWrapper {
 public:
  MessageLoopWrapper();
  ~MessageLoopWrapper();
  debug_ipc::MessageLoop* loop() { return loop_.get(); }

 private:
  std::unique_ptr<debug_ipc::PlatformMessageLoop> loop_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_INTEGRATION_TESTS_MESSAGE_LOOP_WRAPPER_H_
