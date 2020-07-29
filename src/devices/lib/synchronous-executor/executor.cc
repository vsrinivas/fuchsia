// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/synchronous-executor/executor.h>

#include <condition_variable>
#include <mutex>

namespace synchronous_executor {

void synchronous_executor::schedule_task(fit::pending_task task) {
  std::lock_guard<std::mutex> lock(mutex_);
  scheduler_.schedule_task(std::move(task));
}

void synchronous_executor::run_until_idle() {
  // Run until the queue is empty
  while (true) {
    fit::subtle::scheduler::task_queue queue;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      scheduler_.take_runnable_tasks(&queue);
    }
    if (queue.empty()) {
      return;
    }
    while (!queue.empty()) {
      context_impl context(this);
      queue.front()(context);
      auto ticket = context.take_ticket();
      if (ticket.has_value()) {
        std::lock_guard<std::mutex> lock(mutex_);
        scheduler_.finalize_ticket(ticket.value(), &queue.front());
      }
      queue.pop();
    }
  }
}

fit::suspended_task::ticket synchronous_executor::resolver_impl::duplicate_ticket(
    fit::suspended_task::ticket ticket) {
  std::lock_guard<std::mutex> lock(executor_->mutex_);
  executor_->scheduler_.duplicate_ticket(ticket);
  return ticket;
}

// Consumes the provided ticket, optionally resuming its associated task.
// The provided ticket must not be used again.
void synchronous_executor::resolver_impl::resolve_ticket(fit::suspended_task::ticket ticket,
                                                         bool resume_task) {
  fit::pending_task task;
  {
    std::lock_guard<std::mutex> lock(executor_->mutex_);
    if (resume_task) {
      executor_->scheduler_.resume_task_with_ticket(ticket);
    } else {
      task = executor_->scheduler_.release_ticket(ticket);
    }
  }
}

synchronous_executor* synchronous_executor::context_impl::executor() const { return executor_; }

fit::suspended_task synchronous_executor::context_impl::suspend_task() {
  std::lock_guard<std::mutex> lock(executor_->mutex_);
  // One ref for us, another for the promise itself
  ticket_ = executor_->scheduler_.obtain_ticket(2);
  return fit::suspended_task(&executor_->resolver_, ticket_.value());
}

}  // namespace synchronous_executor
