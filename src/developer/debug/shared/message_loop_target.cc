// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/message_loop_target.h"

#include "src/developer/debug/shared/message_loop_async.h"
#include "src/developer/debug/shared/message_loop_zircon.h"

#include "src/lib/fxl/logging.h"

namespace debug_ipc {

MessageLoopTarget::~MessageLoopTarget() = default;

MessageLoopTarget::Type MessageLoopTarget::current_message_loop_type =
    MessageLoopTarget::Type::kLast;

MessageLoopTarget* MessageLoopTarget::Current() {
  FXL_DCHECK(current_message_loop_type != MessageLoopTarget::Type::kLast);
  switch (current_message_loop_type) {
    case MessageLoopTarget::Type::kAsync:
      return MessageLoopAsync::Current();
    case MessageLoopTarget::Type::kZircon:
      return MessageLoopZircon::Current();
    case MessageLoopTarget::Type::kLast:
      break;
  }

  FXL_NOTREACHED();
  return nullptr;
}

const char* MessageLoopTarget::TypeToString(Type type) {
  switch (type) {
    case MessageLoopTarget::Type::kAsync:
      return "Async";
    case MessageLoopTarget::Type::kZircon:
      return "Zircon";
    case MessageLoopTarget::Type::kLast:
      return "Last";
  }

  FXL_NOTREACHED();
  return nullptr;
}

const char* WatchTypeToString(WatchType type) {
  switch (type) {
    case WatchType::kFdio:
      return "FDIO";
    case WatchType::kJobExceptions:
      return "Job";
    case WatchType::kProcessExceptions:
      return "Process";
    case WatchType::kTask:
      return "Task";
    case WatchType::kSocket:
      return "Socket";
  }

  FXL_NOTREACHED();
  return "";
}

}  // namespace debug_ipc
