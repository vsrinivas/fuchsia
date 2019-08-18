// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_CPP_EXECUTOR_H_
#define LIB_ASYNC_CPP_EXECUTOR_H_

#include <mutex>

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/fit/promise.h>
#include <lib/fit/scheduler.h>
#include <lib/fit/thread_safety.h>
#include <lib/zx/time.h>

namespace async {

// Execution context for an asynchronous task that runs within the scope
// of an |async_dispatcher_t|'s dispatch loop, such as a |async::Promise|.
class Context : public fit::context {
public:
    // Gets the executor's |async_dispatcher_t|, never null.
    virtual async_dispatcher_t* dispatcher() const = 0;

protected:
    virtual ~Context() = default;
};

// An asynchronous task executor that wraps an |async_dispatcher_t|.
//
// This allows asynchronous tasks, such as promises, to be evaluated alongside
// other asynchronous operations managed by the |async_dispatcher_t|.
class Executor final : public fit::executor {
public:
    // Wraps the specified dispatcher.
    //
    // |dispatcher| must not be null and it must outlive the executor itself.
    explicit Executor(async_dispatcher_t* dispatcher);

    // Destroys the executor along with all of its remaining scheduled tasks
    // that have yet to complete.
    ~Executor() override;

    // Gets the executor's |async_dispatcher_t|, never null.
    async_dispatcher_t* dispatcher() const { return dispatcher_->dispatcher(); }

    // Schedules a task for eventual execution by the executor.
    //
    // This method is thread-safe.
    void schedule_task(fit::pending_task task) override;

    Executor(const Executor&) = delete;
    Executor(Executor&&) = delete;
    Executor& operator=(const Executor&) = delete;
    Executor& operator=(Executor&&) = delete;

private:
    // The dispatcher runs tasks, provides the suspended task resolver, and
    // provides the task context.
    //
    // The lifetime of this object is somewhat complex since there are pointers
    // to it from multiple sources which are released in different ways.
    //
    // - |Executor| holds a pointer in |dispatcher_| which it releases after
    //   calling |Shutdown()| to inform the dispatcher of its own demise
    // - |suspended_task| holds a pointer to the dispatcher's resolver
    //   interface and the number of outstanding pointers corresponds to the
    //   number of outstanding suspended task tickets tracked by |scheduler_|.
    // - |async_dispatcher_t| holds a pointer to the dispatcher's async task
    //   interface whenever dispatch is pending as indicated by |dispatch_pending_|.
    //
    // The dispatcher deletes itself once all pointers have been released.
    // See also |PurgeTasksAndMaybeDeleteSelfLocked()|.
    class DispatcherImpl final : public fit::suspended_task::resolver,
                                 public async::Context,
                                 public async_task_t {
    public:
        DispatcherImpl(async_dispatcher_t* dispatcher,
                       Executor* executor);

        void Shutdown();
        void ScheduleTask(fit::pending_task task);

        // |executor()| and |dispatcher()| are presented on the |async::Context|
        // so they are only accessible while |task_running_| is true which
        // implies that |executor_| and |dispatcher_| have not been destroyed.
        Executor* executor() const override { return executor_; }
        async_dispatcher_t* dispatcher() const override { return dispatcher_; }

        // Suspends the currently running task.  This method is presented
        // on the |async::Context| so it can only be called while
        // |task_running_| is true as above.
        fit::suspended_task suspend_task() override;

        // These methods implement the suspended task token contract.
        // They may be called on any thread at any time.
        fit::suspended_task::ticket duplicate_ticket(
            fit::suspended_task::ticket ticket) override;
        void resolve_ticket(
            fit::suspended_task::ticket ticket, bool resume_task) override;

    private:
        ~DispatcherImpl() override;

        // Callback from |async_dispatcher_t*|.
        // Invokes |Dispatch()| to run all runnable tasks.
        static void Dispatch(async_dispatcher_t* dispatcher,
                             async_task_t* task, zx_status_t status);
        void Dispatch(zx_status_t status);

        // Runs the specified task.  Called by |Dispatch()|.
        void RunTask(fit::pending_task* task);

        // Attempts to schedule a call to |Dispatch()| on the async dispatcher.
        // Returns true if a dispatch is pending.
        bool ScheduleDispatchLocked() FIT_REQUIRES(guarded_.mutex_);

        // Moves all tasks from |incoming_tasks_| to the |scheduler_| runnable queue.
        void AcceptIncomingTasksLocked() FIT_REQUIRES(guarded_.mutex_);

        // When |was_shutdown_| or |loop_failure_| is true, purges any tasks
        // that remain and deletes the dispatcher if all outstanding references
        // to it have gone away.  Should be called at points where one of these
        // conditions changes.  Takes ownership of the lock and drops it.
        void PurgeTasksAndMaybeDeleteSelfLocked(
            std::unique_lock<std::mutex> lock) FIT_REQUIRES(guarded_.mutex_);

        async_dispatcher_t* const dispatcher_;
        Executor* const executor_;

        // The queue of runnable tasks.
        // Only accessed by |RunTask()| and |suspend_task()| which happens
        // on the dispatch thread.
        fit::subtle::scheduler::task_queue runnable_tasks_;

        // The current suspended task ticket or 0 if none.
        // Only accessed by |RunTask()| and |suspend_task()| which happens
        // on the dispatch thread.
        fit::suspended_task::ticket current_task_ticket_ = 0;

        // A bunch of state that is guarded by a mutex.
        struct {
            std::mutex mutex_;

            // True if the executor is about to be destroyed.
            bool was_shutdown_ FIT_GUARDED(mutex_) = false;

            // True if the underlying async_dispatcher_t reported an error.
            bool loop_failure_ FIT_GUARDED(mutex_) = false;

            // True if a call to |Dispatch()| is pending.
            bool dispatch_pending_ FIT_GUARDED(mutex_) = false;

            // True while |RunTask| is running a task.
            bool task_running_ FIT_GUARDED(mutex_) = false;

            // Holds tasks that have been scheduled on this dispatcher.
            fit::subtle::scheduler scheduler_ FIT_GUARDED(mutex_);

            // Newly scheduled tasks which have yet to be added to the
            // runnable queue.  This allows the dispatch to distinguish between
            // newly scheduled tasks and resumed tasks so it can manage them
            // separately.  See comments in |Dispatch()|.
            fit::subtle::scheduler::task_queue incoming_tasks_ FIT_GUARDED(mutex_);
        } guarded_;
    };

    DispatcherImpl* dispatcher_;
};

} // namespace async

#endif // LIB_ASYNC_CPP_EXECUTOR_H_
