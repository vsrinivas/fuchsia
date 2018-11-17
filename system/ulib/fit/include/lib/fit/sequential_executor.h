// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_SEQUENTIAL_EXECUTOR_H_
#define LIB_FIT_SEQUENTIAL_EXECUTOR_H_

#include <condition_variable>
#include <mutex>
#include <utility>

#include "promise.h"
#include "scheduler.h"
#include "thread_safety.h"

namespace fit {

// A simple platform-independent single-threaded asynchronous task executor.
//
// This implementation is designed for use when writing simple single-threaded
// platform-independent applications.  It may be less efficient or provide
// fewer features than more specialized or platform-dependent executors.
//
// See documentation of |fit::promise| for more information.
class sequential_executor final : public executor {
public:
    sequential_executor();

    // Destroys the executor along with all of its remaining scheduled tasks
    // that have yet to complete.
    ~sequential_executor() override;

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

    sequential_executor(const sequential_executor&) = delete;
    sequential_executor(sequential_executor&&) = delete;
    sequential_executor& operator=(const sequential_executor&) = delete;
    sequential_executor& operator=(sequential_executor&&) = delete;

private:
    // The task context for tasks run by the executor.
    class context_impl final : public context {
    public:
        explicit context_impl(sequential_executor* executor);
        ~context_impl() override;

        sequential_executor* executor() const override;
        suspended_task suspend_task() override;

    private:
        sequential_executor* const executor_;
    };

    // The dispatcher runs tasks and provides the suspended task resolver.
    //
    // The lifetime of this object is somewhat complex since there are pointers
    // to it from multiple sources which are released in different ways.
    //
    // - |sequential_executor| holds a pointer in |dispatcher_| which it releases
    //   after calling |shutdown()| to inform the dispatcher of its own demise
    // - |suspended_task| holds a pointer to the dispatcher's resolver
    //   interface and the number of outstanding pointers corresponds to the
    //   number of outstanding suspended task tickets tracked by |scheduler_|.
    //
    // The dispatcher deletes itself once all pointers have been released.
    class dispatcher_impl final : public suspended_task::resolver {
    public:
        dispatcher_impl();

        void shutdown();
        void schedule_task(pending_task task);
        void run(context_impl& context);
        suspended_task suspend_current_task();

        suspended_task::ticket duplicate_ticket(
            suspended_task::ticket ticket) override;
        void resolve_ticket(
            suspended_task::ticket ticket, bool resume_task) override;

    private:
        ~dispatcher_impl() override;

        void wait_for_runnable_tasks(
            fit::subtle::scheduler::task_queue* out_tasks);
        void run_task(pending_task* task, context& context);

        suspended_task::ticket current_task_ticket_ = 0;
        std::condition_variable wake_;

        // A bunch of state that is guarded by a mutex.
        struct {
            std::mutex mutex_;
            bool was_shutdown_ FIT_GUARDED(mutex_) = false;
            bool need_wake_ FIT_GUARDED(mutex_) = false;
            fit::subtle::scheduler scheduler_ FIT_GUARDED(mutex_);
        } guarded_;
    };

    context_impl context_;
    dispatcher_impl* dispatcher_;
};

// Creates a new |fit::sequential_executor|, schedules a promise as a task,
// runs all of the executor's scheduled tasks until none remain, then returns
// the promise's result.
template <typename Continuation>
static typename promise_impl<Continuation>::result_type
run_sequentially(promise_impl<Continuation> promise) {
    using result_type = typename promise_impl<Continuation>::result_type;
    sequential_executor exec;
    result_type saved_result;
    exec.schedule_task(promise.then([&saved_result](result_type result) {
        saved_result = std::move(result);
    }));
    exec.run();
    return saved_result;
}

} // namespace fit

#endif // LIB_FIT_SEQUENTIAL_EXECUTOR_H_
