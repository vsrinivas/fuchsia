// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_SINGLE_THREADED_EXECUTOR_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_SINGLE_THREADED_EXECUTOR_H_

#include <lib/fasync/future.h>
#include <lib/fasync/scheduler.h>
#include <lib/fit/thread_safety.h>

#include <utility>

namespace fasync {

// A simple platform-independent single-threaded asynchronous task executor.
//
// This implementation is designed for use when writing simple single-threaded platform-independent
// applications. It may be less efficient or provide fewer features than more specialized or
// platform-dependent executors.
//
// See documentation of |fasync::future| for more information.
class single_threaded_executor final : public executor {
 public:
  single_threaded_executor();

  // Destroys the executor along with all of its remaining scheduled tasks that have yet to
  // complete.
  ~single_threaded_executor() override;

  // Schedules a task for eventual execution by the executor.
  //
  // This method is thread-safe.
  void schedule(pending_task&& task) override;

  // Runs all scheduled tasks (including additional tasks scheduled while they run) until none
  // remain.
  //
  // This method is thread-safe but must only be called on at most one thread at a time.
  void run();

  single_threaded_executor(const single_threaded_executor&) = delete;
  single_threaded_executor& operator=(const single_threaded_executor&) = delete;
  single_threaded_executor(single_threaded_executor&&) = delete;
  single_threaded_executor& operator=(single_threaded_executor&&) = delete;

 private:
  // The task context for tasks run by the executor.
  class context_impl final : public context {
   public:
    explicit context_impl(single_threaded_executor& executor) : executor_(executor) {}
    ~context_impl() override = default;

    single_threaded_executor& executor() const override { return executor_; }
    suspended_task suspend_task() override;

   private:
    single_threaded_executor& executor_;
  };

  class dispatcher_impl;

  context_impl context_;
  dispatcher_impl* const dispatcher_;
};

namespace internal {

class block_closure final : future_adaptor_closure<block_closure> {
 public:
  template <typename E = single_threaded_executor, typename F,
            requires_conditions<is_future<F>> = true>
  std::conditional_t<is_void_future_v<F>, bool, cpp17::optional<future_output_t<F>>> operator()(
      F&& future) const {
    E executor;
    return block_on(std::forward<F>(future), executor);
  }
};

}  // namespace internal

// |fasync::block|
//
// Creates a new |fasync::single_threaded_executor| (by default), schedules a future as a task,
// runs all of the executor's scheduled tasks until none remain, then returns
// the future's result.
//
// Call pattern:
// - fasync::block(<future>) -> std::optional<result> (or bool for void futures)
// - <future> | fasync::block -> std::optional<result> (or bool)
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::block_closure block;

}  // namespace fasync

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_SINGLE_THREADED_EXECUTOR_H_
