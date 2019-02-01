// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"

namespace debug_agent {

// Inits a message loop on construction and clears it on destruction.
class MessageLoopWrapper {
 public:
  MessageLoopWrapper();
  ~MessageLoopWrapper();
  debug_ipc::MessageLoop* loop() { return &loop_; }

 private:
  debug_ipc::MessageLoopZircon loop_;
};

}  // namespace debug_agent
