// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fpromise/bridge.h>
#include <zircon/assert.h>

namespace async {

Executor::Executor(async_dispatcher_t* dispatcher)
    : dispatcher_(new DispatcherImpl(dispatcher, this)) {}

Executor::~Executor() { dispatcher_->Shutdown(); }

void Executor::schedule_task(fpromise::pending_task task) {
  ZX_DEBUG_ASSERT(task);
  dispatcher_->ScheduleTask(std::move(task));
}

fpromise::promise<> Executor::MakeDelayedPromise(zx::duration duration) {
  fpromise::bridge<> bridge;
  async::PostDelayedTask(
      dispatcher(),
      [completer = std::move(bridge.completer)]() mutable { completer.complete_ok(); }, duration);

  return bridge.consumer.promise();
}

fpromise::promise<> Executor::MakePromiseForTime(zx::time deadline) {
  fpromise::bridge<> bridge;
  async::PostTaskForTime(
      dispatcher(),
      [completer = std::move(bridge.completer)]() mutable { completer.complete_ok(); }, deadline);

  return bridge.consumer.promise();
}

fpromise::promise<zx_packet_signal_t, zx_status_t> Executor::MakePromiseWaitHandle(
    zx::unowned_handle object, zx_signals_t trigger, uint32_t options) {
  fpromise::bridge<zx_packet_signal_t, zx_status_t> bridge;
  auto wait_once = std::make_unique<async::WaitOnce>(object->get(), trigger, options);
  auto wait_once_raw = wait_once.get();
  wait_once_raw->Begin(
      dispatcher(), [wait_once = std::move(wait_once), completer = std::move(bridge.completer)](
                        async_dispatcher_t* dispatcher, async::WaitOnce* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) mutable {
        if (status == ZX_OK) {
          ZX_DEBUG_ASSERT(signal);
          completer.complete_ok(*signal);
        } else {
          completer.complete_error(status);
        }
      });

  return bridge.consumer.promise();
}

Executor::DispatcherImpl::DispatcherImpl(async_dispatcher_t* dispatcher, Executor* executor)
    : async_task_t{{ASYNC_STATE_INIT}, &DispatcherImpl::Dispatch, 0},
      dispatcher_(dispatcher),
      executor_(executor) {
  ZX_DEBUG_ASSERT(dispatcher_ != nullptr);
  ZX_DEBUG_ASSERT(executor_ != nullptr);
}

Executor::DispatcherImpl::~DispatcherImpl() {
  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  ZX_DEBUG_ASSERT(guarded_.was_shutdown_);
  ZX_DEBUG_ASSERT(!guarded_.dispatch_pending_);
  ZX_DEBUG_ASSERT(!guarded_.scheduler_.has_runnable_tasks());
  ZX_DEBUG_ASSERT(!guarded_.scheduler_.has_suspended_tasks());
  ZX_DEBUG_ASSERT(!guarded_.scheduler_.has_outstanding_tickets());
  ZX_DEBUG_ASSERT(guarded_.incoming_tasks_.empty());
  ZX_DEBUG_ASSERT(!guarded_.task_running_);
}

// Unfortunately std::unique_lock does not support thread-safety annotations
void Executor::DispatcherImpl::Shutdown() FIT_NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock<std::mutex> lock(guarded_.mutex_);
  ZX_DEBUG_ASSERT(!guarded_.was_shutdown_);
  ZX_ASSERT_MSG(!guarded_.task_running_,
                "async::Executor must not be destroyed while tasks may "
                "be running concurrently on the dispatcher because the "
                "task's context holds a pointer to the executor.");
  guarded_.was_shutdown_ = true;
  PurgeTasksAndMaybeDeleteSelfLocked(std::move(lock));
}

void Executor::DispatcherImpl::ScheduleTask(fpromise::pending_task task) {
  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  ZX_DEBUG_ASSERT(!guarded_.was_shutdown_);

  // Try to post the task first.
  // This may fail if the loop is being shut down, in which case we
  // will let the task be destroyed once it goes out of scope.
  if (!guarded_.loop_failure_ && ScheduleDispatchLocked()) {
    guarded_.incoming_tasks_.push(std::move(task));
  }  // else drop the task once the function returns
}

void Executor::DispatcherImpl::Dispatch(async_dispatcher_t* dispatcher, async_task_t* task,
                                        zx_status_t status) {
  DispatcherImpl* self = static_cast<DispatcherImpl*>(task);
  self->Dispatch(status);
}

// Unfortunately std::unique_lock does not support thread-safety annotations
void Executor::DispatcherImpl::Dispatch(zx_status_t status) FIT_NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock<std::mutex> lock(guarded_.mutex_);
  ZX_DEBUG_ASSERT(guarded_.dispatch_pending_);
  ZX_DEBUG_ASSERT(!guarded_.loop_failure_);
  ZX_DEBUG_ASSERT(!guarded_.task_running_);

  if (status == ZX_OK) {
    // Accept incoming tasks only once before entering the loop.
    //
    // This ensures that each invocation of |Dispatch()| has a bounded
    // amount of work to perform.  Specifically, it will only execute
    // incoming tasks, tasks that are already runnable, and tasks that are
    // currently suspended but become runnable while the loop is executing.
    // Once finished, the loop returns control back to the async dispatcher.
    //
    // The purpose of this deconstruction is to prevent other units of work
    // scheduled by the async dispatcher from being starved in the event
    // that there is a continuous stream of new tasks being scheduled on the
    // executor.  As an extreme example, we must ensure that the async
    // dispatcher has an opportunity to process its own quit message and
    // shut down in that scenario.
    //
    // An alternative way to solve this problem would be to not loop at all.
    // Unfortunately, that would significantly increase the overhead of
    // processing tasks resumed by other tasks.
    AcceptIncomingTasksLocked();
    while (!guarded_.was_shutdown_) {
      guarded_.scheduler_.take_runnable_tasks(&runnable_tasks_);
      if (runnable_tasks_.empty()) {
        guarded_.dispatch_pending_ = false;
        if (guarded_.incoming_tasks_.empty() || ScheduleDispatchLocked()) {
          return;  // all done
        }
        break;  // a loop failure occurred, we need to clean up
      }

      // Drop lock while running tasks then reaquire it.
      guarded_.task_running_ = true;
      lock.unlock();
      do {
        RunTask(&runnable_tasks_.front());
        runnable_tasks_.pop();  // the task may be destroyed here if it was not suspended
      } while (!runnable_tasks_.empty());
      lock.lock();
      guarded_.task_running_ = false;
    }
  } else {
    guarded_.loop_failure_ = true;
  }
  guarded_.dispatch_pending_ = false;
  PurgeTasksAndMaybeDeleteSelfLocked(std::move(lock));
}

void Executor::DispatcherImpl::RunTask(fpromise::pending_task* task) {
  ZX_DEBUG_ASSERT(current_task_ticket_ == 0);
  const bool finished = (*task)(*this);
  ZX_DEBUG_ASSERT(!*task == finished);
  if (current_task_ticket_ == 0) {
    return;  // task was not suspended, no ticket was produced
  }

  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  guarded_.scheduler_.finalize_ticket(current_task_ticket_, task);
  current_task_ticket_ = 0;
}

// Must only be called while |run_task()| is running a task.
// This happens when the task's continuation calls |context::suspend_task()|
// upon the context it received as an argument.
fpromise::suspended_task Executor::DispatcherImpl::suspend_task() {
  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  ZX_DEBUG_ASSERT(guarded_.task_running_);
  if (current_task_ticket_ == 0) {
    current_task_ticket_ = guarded_.scheduler_.obtain_ticket(2 /*initial_refs*/);
  } else {
    guarded_.scheduler_.duplicate_ticket(current_task_ticket_);
  }
  return fpromise::suspended_task(this, current_task_ticket_);
}

fpromise::suspended_task::ticket Executor::DispatcherImpl::duplicate_ticket(
    fpromise::suspended_task::ticket ticket) {
  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  guarded_.scheduler_.duplicate_ticket(ticket);
  return ticket;
}

// Unfortunately std::unique_lock does not support thread-safety annotations
void Executor::DispatcherImpl::resolve_ticket(fpromise::suspended_task::ticket ticket,
                                              bool resume_task) FIT_NO_THREAD_SAFETY_ANALYSIS {
  fpromise::pending_task abandoned_task;  // drop outside of the lock
  {
    std::unique_lock<std::mutex> lock(guarded_.mutex_);
    bool did_resume = false;
    if (resume_task) {
      did_resume = guarded_.scheduler_.resume_task_with_ticket(ticket);
    } else {
      abandoned_task = guarded_.scheduler_.release_ticket(ticket);
    }
    if (!guarded_.was_shutdown_ && !guarded_.loop_failure_ &&
        (!did_resume || ScheduleDispatchLocked())) {
      return;  // all done
    }
    PurgeTasksAndMaybeDeleteSelfLocked(std::move(lock));
  }
}

bool Executor::DispatcherImpl::ScheduleDispatchLocked() {
  ZX_DEBUG_ASSERT(!guarded_.was_shutdown_ && !guarded_.loop_failure_);
  if (guarded_.dispatch_pending_) {
    return true;  // nothing to do
  }
  zx_status_t status = async_post_task(dispatcher_, this);
  ZX_ASSERT_MSG(status == ZX_OK || status == ZX_ERR_BAD_STATE, "status=%d", status);
  if (status == ZX_OK) {
    guarded_.dispatch_pending_ = true;
    return true;  // everything's ok
  }
  guarded_.loop_failure_ = true;
  return false;  // failed
}

void Executor::DispatcherImpl::AcceptIncomingTasksLocked() {
  while (!guarded_.incoming_tasks_.empty()) {
    guarded_.scheduler_.schedule_task(std::move(guarded_.incoming_tasks_.front()));
    guarded_.incoming_tasks_.pop();
  }
}

// Unfortunately std::unique_lock does not support thread-safety annotations
void Executor::DispatcherImpl::PurgeTasksAndMaybeDeleteSelfLocked(std::unique_lock<std::mutex> lock)
    FIT_NO_THREAD_SAFETY_ANALYSIS {
  ZX_DEBUG_ASSERT(lock.owns_lock());
  ZX_DEBUG_ASSERT(guarded_.was_shutdown_ || guarded_.loop_failure_);

  fpromise::subtle::scheduler::task_queue tasks;
  AcceptIncomingTasksLocked();
  guarded_.scheduler_.take_all_tasks(&tasks);
  const bool can_delete_self = guarded_.was_shutdown_ && !guarded_.dispatch_pending_ &&
                               !guarded_.scheduler_.has_outstanding_tickets();

  lock.unlock();

  if (can_delete_self) {
    delete this;
  }
}

}  // namespace async
