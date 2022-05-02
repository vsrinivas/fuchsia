// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_FEXECUTOR_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_FEXECUTOR_H_

#include <lib/fasync/internal/compiler.h>

LIB_FASYNC_CPP_VERSION_COMPAT_BEGIN

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/fasync/future.h>
#include <lib/fasync/scheduler.h>
#include <lib/fit/thread_safety.h>
#include <lib/zx/handle.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <mutex>

namespace fasync {

// Execution context for an asynchronous task that runs within the scope
// of an |async_dispatcher_t|'s dispatch loop, such as an |fasync::future|.
class fcontext : public fasync::context {
 public:
  // Gets the executor's |async_dispatcher_t|, never null.
  virtual async_dispatcher_t* dispatcher() const = 0;

 protected:
  ~fcontext() override = default;
};

// An asynchronous task executor that wraps an |async_dispatcher_t|.
//
// This allows asynchronous tasks, such as futures, to be evaluated alongside
// other asynchronous operations managed by the |async_dispatcher_t|.
class fexecutor final : public fasync::executor {
 public:
  // Wraps the specified dispatcher.
  //
  // |dispatcher| must not be null and it must outlive the executor itself.
  explicit fexecutor(async_dispatcher_t* dispatcher)
      : dispatcher_(new dispatcher_impl(dispatcher, *this)) {}

  // Destroys the executor along with all of its remaining scheduled tasks
  // that have yet to complete.
  ~fexecutor() override { dispatcher_->shutdown(); }

  // Gets the executor's |async_dispatcher_t|, never null.
  async_dispatcher_t* dispatcher() const { return dispatcher_->dispatcher(); }

  // Schedules a task for eventual execution by the executor.
  //
  // This method is thread-safe.
  void schedule(fasync::pending_task&& task) override { dispatcher_->schedule(std::move(task)); }

  fexecutor(const fexecutor&) = delete;
  fexecutor& operator=(const fexecutor&) = delete;
  fexecutor(fexecutor&&) = delete;
  fexecutor& operator=(fexecutor&&) = delete;

  // Returns a future that will complete after the specified duration.
  //
  // The countdown starts when this method is called.
  fasync::try_future<zx_status_t> make_delayed_future(zx::duration duration);

  // Returns a future that will complete on or after |deadline|.
  //
  // The countdown starts when this method is called.
  fasync::try_future<zx_status_t> make_future_for_time(zx::time deadline);

  // Makes a future that waits for one or more signals on a handle.
  //
  // |object|, |trigger|, and |options| must be valid according to the
  // corresponding arguments to |async::WaitOnce()|.
  //
  // |object| must remain valid at least until |trigger| is sent. The returned future will only
  // have access to the data that was sent up to the point that |object| received |trigger|.
  fasync::try_future<zx_status_t, zx_packet_signal_t> make_future_wait_for_handle(
      zx::unowned_handle object, zx_signals_t trigger = ZX_SIGNAL_NONE, uint32_t options = 0);

 private:
  // The dispatcher runs tasks, provides the suspended task resolver, and
  // provides the task context.
  //
  // The lifetime of this object is somewhat complex since there are pointers
  // to it from multiple sources which are released in different ways.
  //
  // - |fexecutor| holds a pointer in |dispatcher_| which it releases after
  //   calling |shutdown()| to inform the dispatcher of its own demise
  // - |suspended_task| holds a pointer to the dispatcher's resolver
  //   interface and the number of outstanding pointers corresponds to the
  //   number of outstanding suspended task tickets tracked by |scheduler_|.
  // - |async_dispatcher_t| holds a pointer to the dispatcher's async task
  //   interface whenever dispatch is pending as indicated by |dispatch_pending_|.
  //
  // The dispatcher deletes itself once all pointers have been released.
  // See also |purge_tasks_and_maybe_delete_self_locked()|.
  class dispatcher_impl final : public fasync::suspended_task::resolver,
                                public fasync::fcontext,
                                public async_task_t {
   public:
    dispatcher_impl(async_dispatcher_t* dispatcher, fexecutor& executor)
        : async_task_t{{ASYNC_STATE_INIT}, &dispatcher_impl::dispatch, 0},
          dispatcher_(dispatcher),
          executor_(executor) {
      ZX_DEBUG_ASSERT(dispatcher_ != nullptr);
    }

    void shutdown();
    void schedule(fasync::pending_task&& task);

    // |executor()| and |dispatcher()| are presented on the |fasync::fcontext|
    // so they are only accessible while |task_running_| is true which
    // implies that |executor_| and |dispatcher_| have not been destroyed.
    fexecutor& executor() const override { return executor_; }
    async_dispatcher_t* dispatcher() const override { return dispatcher_; }

    // Suspends the currently running task.  This method is presented
    // on the |fasync::fcontext| so it can only be called while
    // |task_running_| is true as above.
    fasync::suspended_task suspend_task() override;

    // These methods implement the suspended task token contract.
    // They may be called on any thread at any time.
    fasync::suspended_task::ticket duplicate_ticket(fasync::suspended_task::ticket ticket) override;
    void resolve_ticket(fasync::suspended_task::ticket ticket, bool resume_task) override;

   private:
    ~dispatcher_impl() override {
      std::lock_guard<std::mutex> lock(guarded_.mutex_);
      ZX_DEBUG_ASSERT(guarded_.was_shutdown_);
      ZX_DEBUG_ASSERT(!guarded_.dispatch_pending_);
      ZX_DEBUG_ASSERT(!guarded_.scheduler_.has_runnable_tasks());
      ZX_DEBUG_ASSERT(!guarded_.scheduler_.has_suspended_tasks());
      ZX_DEBUG_ASSERT(!guarded_.scheduler_.has_outstanding_tickets());
      ZX_DEBUG_ASSERT(guarded_.incoming_tasks_.empty());
      ZX_DEBUG_ASSERT(!guarded_.task_running_);
    }

    // Callback from |async_dispatcher_t*|.
    // Invokes |dispatch()| to run all runnable tasks.
    static void dispatch(async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status) {
      dispatcher_impl* self = static_cast<dispatcher_impl*>(task);
      self->dispatch(status);
    }

    void dispatch(zx_status_t status);

    // Runs the specified task.  Called by |dispatch()|.
    void run_task(fasync::pending_task& task);

    // Attempts to schedule a call to |dispatch()| on the async dispatcher.
    // Returns true if a dispatch is pending.
    bool schedule_dispatch_locked() FIT_REQUIRES(guarded_.mutex_);

    // Moves all tasks from |incoming_tasks_| to the |scheduler_| runnable queue.
    void accept_incoming_tasks_locked() FIT_REQUIRES(guarded_.mutex_);

    // When |was_shutdown_| or |loop_failure_| is true, purges any tasks
    // that remain and deletes the dispatcher if all outstanding references
    // to it have gone away.  Should be called at points where one of these
    // conditions changes.  Takes ownership of the lock and drops it.
    void purge_tasks_and_maybe_delete_self_locked(std::unique_lock<std::mutex> lock)
        FIT_REQUIRES(guarded_.mutex_);

    async_dispatcher_t* const dispatcher_;
    fexecutor& executor_;

    // The queue of runnable tasks.
    // Only accessed by |run_task()| and |suspend_task()| which happens
    // on the dispatch thread.
    fasync::subtle::scheduler::task_queue runnable_tasks_;

    // The current suspended task ticket or 0 if none.
    // Only accessed by |run_task()| and |suspend_task()| which happens
    // on the dispatch thread.
    fasync::suspended_task::ticket current_task_ticket_ = 0;

    // A bunch of state that is guarded by a mutex.
    struct {
      std::mutex mutex_;

      // True if the executor is about to be destroyed.
      bool was_shutdown_ FIT_GUARDED(mutex_) = false;

      // True if the underlying async_dispatcher_t reported an error.
      bool loop_failure_ FIT_GUARDED(mutex_) = false;

      // True if a call to |dispatch()| is pending.
      bool dispatch_pending_ FIT_GUARDED(mutex_) = false;

      // True while |run_task()| is running a task.
      bool task_running_ FIT_GUARDED(mutex_) = false;

      // Holds tasks that have been scheduled on this dispatcher.
      fasync::subtle::scheduler scheduler_ FIT_GUARDED(mutex_);

      // Newly scheduled tasks which have yet to be added to the
      // runnable queue.  This allows the dispatch to distinguish between
      // newly scheduled tasks and resumed tasks so it can manage them
      // separately.  See comments in |dispatch()|.
      fasync::subtle::scheduler::task_queue incoming_tasks_ FIT_GUARDED(mutex_);
    } guarded_;
  };

  dispatcher_impl* dispatcher_;
};

}  // namespace fasync

LIB_FASYNC_CPP_VERSION_COMPAT_END

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_FEXECUTOR_H_
