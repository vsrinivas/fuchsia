// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler_manager.h"

namespace forensics {
namespace exceptions {

HandlerManager::HandlerManager(async_dispatcher_t* dispatcher, size_t max_num_handlers,
                               zx::duration exception_ttl)
    : dispatcher_(dispatcher), exception_ttl_(exception_ttl) {
  handlers_.reserve(max_num_handlers);
  for (size_t i = 0; i < max_num_handlers; ++i) {
    handlers_.emplace_back(dispatcher_, /*on_available=*/[i, this] {
      // Push to the front so already initialized handlers are used first.
      available_handlers_.push_front(i);
      HandleNextPendingException();
    });
    available_handlers_.emplace_back(i);
  }
}

void HandlerManager::Handle(zx::exception exception) {
  pending_exceptions_.emplace_back(dispatcher_, exception_ttl_, std::move(exception));
  HandleNextPendingException();
}

void HandlerManager::HandleNextPendingException() {
  if (pending_exceptions_.empty() || available_handlers_.empty()) {
    return;
  }

  // We must reserve all state needed to handle the exception (the handler and the exception) and
  // remove it from the queues prior to actually handling the exception. This is done to prevent
  // that state from being erroneously being reused when ProcessHandler::Handle ends up calling
  // HandleNextPendingException on a failure.
  const size_t handler_idx = available_handlers_.front();
  available_handlers_.pop_front();

  const std::string crashed_process_name = pending_exceptions_.front().CrashedProcessName();
  const zx_koid_t crashed_thread_koid = pending_exceptions_.front().CrashedThreadKoid();
  zx::exception exception = pending_exceptions_.front().TakeException();
  pending_exceptions_.pop_front();

  handlers_[handler_idx].Handle(crashed_process_name, crashed_thread_koid, std::move(exception));
}

}  // namespace exceptions
}  // namespace forensics
