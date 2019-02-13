// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/message_loop_target.h"

#include "garnet/lib/debug_ipc/helper/message_loop_async.h"
#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"

#include "lib/fxl/logging.h"

namespace debug_ipc {

MessageLoopTarget::~MessageLoopTarget() = default;

MessageLoopTarget::LoopType MessageLoopTarget::current_message_loop_type =
    MessageLoopTarget::LoopType::kLast;

MessageLoopTarget* MessageLoopTarget::Current() {
  FXL_DCHECK(current_message_loop_type != MessageLoopTarget::LoopType::kLast);
  switch (current_message_loop_type) {
    case MessageLoopTarget::LoopType::kAsync:
      return MessageLoopAsync::Current();
    case MessageLoopTarget::LoopType::kZircon:
      return MessageLoopZircon::Current();
    case MessageLoopTarget::LoopType::kLast:
      break;
  }

  FXL_NOTREACHED();
  return nullptr;
}

const char* MessageLoopTarget::LoopTypeToString(LoopType type) {
  switch (type) {
    case MessageLoopTarget::LoopType::kAsync:
      return "Async";
    case MessageLoopTarget::LoopType::kZircon:
      return "Zircon";
    case MessageLoopTarget::LoopType::kLast:
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
