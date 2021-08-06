// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/integration_tests/message_loop_wrapper.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/shared/platform_message_loop.h"

namespace debug_agent {

MessageLoopWrapper::MessageLoopWrapper() {
  loop_ = std::make_unique<debug::PlatformMessageLoop>();

  std::string error_message;
  bool success = loop_->Init(&error_message);
  FX_CHECK(success) << error_message;
}

MessageLoopWrapper::~MessageLoopWrapper() { loop_->Cleanup(); }

}  // namespace debug_agent
