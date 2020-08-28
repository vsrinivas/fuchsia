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

  handlers_[available_handlers_.front()].Handle(pending_exceptions_.front());

  pending_exceptions_.pop_front();
  available_handlers_.pop_front();
}

}  // namespace exceptions
}  // namespace forensics
