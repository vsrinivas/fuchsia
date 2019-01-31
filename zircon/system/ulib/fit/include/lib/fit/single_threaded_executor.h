// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_SINGLE_THREADED_EXECUTOR_H_
#define LIB_FIT_SINGLE_THREADED_EXECUTOR_H_

#include <utility>

#include "promise.h"
#include "scheduler.h"

namespace fit {

// A simple platform-independent single-threaded asynchronous task executor.
//
// This implementation is designed for use when writing simple single-threaded
// platform-independent applications.  It may be less efficient or provide
// fewer features than more specialized or platform-dependent executors.
//
// See documentation of |fit::promise| for more information.
class single_threaded_executor final : public executor {
public:
    single_threaded_executor();

    // Destroys the executor along with all of its remaining scheduled tasks
    // that have yet to complete.
    ~single_threaded_executor() override;

    // Schedules a task for eventual execution by the executor.
    //
    // This method is thread-safe.
    void schedule_task(pending_task task) override;

    // Runs all scheduled tasks (including additional tasks scheduled while
    // they run) until none remain.
    //
    // This method is thread-safe but must only be called on at most one
    // thread at a time.
    void run();

    single_threaded_executor(const single_threaded_executor&) = delete;
    single_threaded_executor(single_threaded_executor&&) = delete;
    single_threaded_executor& operator=(const single_threaded_executor&) = delete;
    single_threaded_executor& operator=(single_threaded_executor&&) = delete;

private:
    class dispatcher_impl;

    // The task context for tasks run by the executor.
    class context_impl final : public context {
    public:
        explicit context_impl(single_threaded_executor* executor);
        ~context_impl() override;

        single_threaded_executor* executor() const override;
        suspended_task suspend_task() override;

    private:
        single_threaded_executor* const executor_;
    };

    context_impl context_;
    dispatcher_impl* const dispatcher_;
};

// Creates a new |fit::single_threaded_executor|, schedules a promise as a task,
// runs all of the executor's scheduled tasks until none remain, then returns
// the promise's result.
template <typename Continuation>
static typename promise_impl<Continuation>::result_type
run_single_threaded(promise_impl<Continuation> promise) {
    using result_type = typename promise_impl<Continuation>::result_type;
    single_threaded_executor exec;
    result_type saved_result;
    exec.schedule_task(promise.then([&saved_result](result_type result) {
        saved_result = std::move(result);
    }));
    exec.run();
    return saved_result;
}

} // namespace fit

#endif // LIB_FIT_SINGLE_THREADED_EXECUTOR_H_
