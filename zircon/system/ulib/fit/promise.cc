// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/promise.h>

namespace fit {

suspended_task::suspended_task(const suspended_task& other)
    : resolver_(other.resolver_),
      ticket_(resolver_ ? resolver_->duplicate_ticket(other.ticket_) : 0) {}

suspended_task::suspended_task(suspended_task&& other)
    : resolver_(other.resolver_), ticket_(other.ticket_) {
  other.resolver_ = nullptr;
}

suspended_task::~suspended_task() { reset(); }

void suspended_task::resolve(bool resume_task) {
  if (resolver_) {
    // Move the ticket to the stack to guard against possible re-entrance
    // occurring as a side-effect of the task's own destructor running.
    resolver* cached_resolver = resolver_;
    ticket cached_ticket = ticket_;
    resolver_ = nullptr;
    cached_resolver->resolve_ticket(cached_ticket, resume_task);
  }
}

suspended_task& suspended_task::operator=(const suspended_task& other) {
  if (this != &other) {
    reset();
    resolver_ = other.resolver_;
    ticket_ = resolver_ ? resolver_->duplicate_ticket(other.ticket_) : 0;
  }
  return *this;
}

suspended_task& suspended_task::operator=(suspended_task&& other) {
  if (this != &other) {
    reset();
    resolver_ = other.resolver_;
    ticket_ = other.ticket_;
    other.resolver_ = nullptr;
  }
  return *this;
}

void suspended_task::swap(suspended_task& other) {
  if (this != &other) {
    using std::swap;
    swap(resolver_, other.resolver_);
    swap(ticket_, other.ticket_);
  }
}

}  // namespace fit
