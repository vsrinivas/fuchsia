// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_SYNCHRONOUS_EXECUTOR_INCLUDE_LIB_SYNCHRONOUS_EXECUTOR_EXECUTOR_H_
#define SRC_DEVICES_LIB_SYNCHRONOUS_EXECUTOR_INCLUDE_LIB_SYNCHRONOUS_EXECUTOR_EXECUTOR_H_

#include <lib/fit/thread_safety.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/scheduler.h>

#include <mutex>
#include <utility>

namespace synchronous_executor {

// A simple synchronous executor that immediately executes all the tasks in its
// run queue when invoked. Rather than blocking for new tasks, it will stop
// once its queue is empty. It is also re-entrant (may safely call run
// from inside a task on the executor).
//
// See documentation of |fpromise::promise| for more information.
class synchronous_executor final : public fpromise::executor {
 public:
  synchronous_executor() : resolver_(this) {}

  // Destroys the executor along with all of its remaining scheduled tasks
  // that have yet to complete.
  ~synchronous_executor() = default;

  // Schedules a task for eventual execution by the executor.
  //
  // This method is thread-safe.
  void schedule_task(fpromise::pending_task task) override;

  // Runs all scheduled tasks (including additional tasks scheduled while
  // they run) until none remain. Tasks executed from run may safely call run
  // reentrantly.
  //
  // This method is thread-safe.
  void run_until_idle();

  synchronous_executor(synchronous_executor&&) = delete;
  synchronous_executor& operator=(synchronous_executor&&) = delete;

 private:
  class resolver_impl : public fpromise::suspended_task::resolver {
   public:
    explicit resolver_impl(synchronous_executor* executor) : executor_(executor) {}

    fpromise::suspended_task::ticket duplicate_ticket(
        fpromise::suspended_task::ticket ticket) override;

    // Consumes the provided ticket, optionally resuming its associated task.
    // The provided ticket must not be used again.
    void resolve_ticket(fpromise::suspended_task::ticket ticket, bool resume_task) override;

   private:
    synchronous_executor* const executor_;
  };
  // The task context for tasks run by the executor.
  class context_impl final : public fpromise::context {
   public:
    explicit context_impl(synchronous_executor* executor) : executor_(executor) {}
    ~context_impl() override = default;

    synchronous_executor* executor() const override;
    fpromise::suspended_task suspend_task() override;
    std::optional<fpromise::suspended_task::ticket> take_ticket() {
      auto ticket = ticket_;
      ticket_.reset();
      return ticket;
    }

   private:
    std::optional<fpromise::suspended_task::ticket> ticket_;
    synchronous_executor* const executor_;
  };
  fpromise::subtle::scheduler scheduler_ FIT_GUARDED(mutex_);
  resolver_impl resolver_;
  std::mutex mutex_;
};

}  // namespace synchronous_executor

#endif  // SRC_DEVICES_LIB_SYNCHRONOUS_EXECUTOR_INCLUDE_LIB_SYNCHRONOUS_EXECUTOR_EXECUTOR_H_
