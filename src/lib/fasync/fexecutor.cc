// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fasync/bridge.h>
#include <lib/fasync/fexecutor.h>
#include <lib/stdcompat/optional.h>
#include <zircon/assert.h>

namespace fasync {

fasync::try_future<zx_status_t> fexecutor::make_delayed_future(zx::duration duration) {
  fasync::bridge<zx_status_t> bridge;
  async::PostDelayedTask(
      dispatcher(),
      [completer = std::move(bridge.completer)]() mutable { completer.complete_ok(); }, duration);

  return bridge.consumer.future_or(fit::as_error(ZX_ERR_CANCELED));
}

fasync::try_future<zx_status_t> fexecutor::make_future_for_time(zx::time deadline) {
  fasync::bridge<zx_status_t> bridge;
  async::PostTaskForTime(
      dispatcher(),
      [completer = std::move(bridge.completer)]() mutable { completer.complete_ok(); }, deadline);

  return bridge.consumer.future_or(fit::as_error(ZX_ERR_CANCELED));
}

fasync::try_future<zx_status_t, zx_packet_signal_t> fexecutor::make_future_wait_for_handle(
    zx::unowned_handle object, zx_signals_t trigger, uint32_t options) {
  fasync::bridge<zx_status_t, zx_packet_signal_t> bridge;
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

  return bridge.consumer.future_or(fit::as_error(ZX_ERR_CANCELED));
}

// Unfortunately std::unique_lock does not support thread-safety annotations
void fexecutor::dispatcher_impl::shutdown() FIT_NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock<std::mutex> lock(guarded_.mutex_);
  ZX_DEBUG_ASSERT(!guarded_.was_shutdown_);
  ZX_ASSERT_MSG(!guarded_.task_running_,
                "async::fexecutor must not be destroyed while tasks may "
                "be running concurrently on the dispatcher because the "
                "task's context holds a pointer to the executor.");
  guarded_.was_shutdown_ = true;
  purge_tasks_and_maybe_delete_self_locked(std::move(lock));
}

void fexecutor::dispatcher_impl::schedule(fasync::pending_task&& task) {
  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  ZX_DEBUG_ASSERT(!guarded_.was_shutdown_);

  // Try to post the task first.
  // This may fail if the loop is being shut down, in which case we
  // will let the task be destroyed once it goes out of scope.
  if (!guarded_.loop_failure_ && schedule_dispatch_locked()) {
    guarded_.incoming_tasks_.push(std::move(task));
  }  // else drop the task once the function returns
}

// Unfortunately std::unique_lock does not support thread-safety annotations
void fexecutor::dispatcher_impl::dispatch(zx_status_t status) {
  std::unique_lock<std::mutex> lock(guarded_.mutex_);
  []() FIT_THREAD_ANNOTATION(__assert_capability__(guarded_.mutex_)) {}();
  ZX_DEBUG_ASSERT(guarded_.dispatch_pending_);
  ZX_DEBUG_ASSERT(!guarded_.loop_failure_);
  ZX_DEBUG_ASSERT(!guarded_.task_running_);

  if (status == ZX_OK) {
    // Accept incoming tasks only once before entering the loop.
    //
    // This ensures that each invocation of |dispatch()| has a bounded
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
    accept_incoming_tasks_locked();
    while (!guarded_.was_shutdown_) {
      runnable_tasks_ = guarded_.scheduler_.take_runnable_tasks();
      if (runnable_tasks_.empty()) {
        guarded_.dispatch_pending_ = false;
        if (guarded_.incoming_tasks_.empty() || schedule_dispatch_locked()) {
          return;  // all done
        }
        break;  // a loop failure occurred, we need to clean up
      }

      // Drop lock while running tasks then reaquire it.
      guarded_.task_running_ = true;
      lock.unlock();
      do {
        run_task(runnable_tasks_.front());
        runnable_tasks_.pop();  // the task may be destroyed here if it was not suspended
      } while (!runnable_tasks_.empty());
      lock.lock();
      guarded_.task_running_ = false;
    }
  } else {
    guarded_.loop_failure_ = true;
  }
  guarded_.dispatch_pending_ = false;
  purge_tasks_and_maybe_delete_self_locked(std::move(lock));
}

void fexecutor::dispatcher_impl::run_task(fasync::pending_task& task) {
  ZX_DEBUG_ASSERT(current_task_ticket_ == 0);
  task(*this);
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
fasync::suspended_task fexecutor::dispatcher_impl::suspend_task() {
  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  ZX_DEBUG_ASSERT(guarded_.task_running_);
  if (current_task_ticket_ == 0) {
    current_task_ticket_ = guarded_.scheduler_.obtain_ticket(2 /*initial_refs*/);
  } else {
    guarded_.scheduler_.duplicate_ticket(current_task_ticket_);
  }
  return fasync::suspended_task(*this, current_task_ticket_);
}

fasync::suspended_task::ticket fexecutor::dispatcher_impl::duplicate_ticket(
    fasync::suspended_task::ticket ticket) {
  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  guarded_.scheduler_.duplicate_ticket(ticket);
  return ticket;
}

// Unfortunately std::unique_lock does not support thread-safety annotations
void fexecutor::dispatcher_impl::resolve_ticket(fasync::suspended_task::ticket ticket,
                                                bool resume_task) FIT_NO_THREAD_SAFETY_ANALYSIS {
  cpp17::optional<fasync::pending_task> abandoned_task;  // drop outside of the lock
  {
    std::unique_lock<std::mutex> lock(guarded_.mutex_);
    bool did_resume = false;
    if (resume_task) {
      did_resume = guarded_.scheduler_.resume_task_with_ticket(ticket);
    } else {
      abandoned_task = guarded_.scheduler_.release_ticket(ticket);
    }
    if (!guarded_.was_shutdown_ && !guarded_.loop_failure_ &&
        (!did_resume || schedule_dispatch_locked())) {
      return;  // all done
    }
    purge_tasks_and_maybe_delete_self_locked(std::move(lock));
  }
}

bool fexecutor::dispatcher_impl::schedule_dispatch_locked() {
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

void fexecutor::dispatcher_impl::accept_incoming_tasks_locked() {
  while (!guarded_.incoming_tasks_.empty()) {
    guarded_.scheduler_.schedule(std::move(guarded_.incoming_tasks_.front()));
    guarded_.incoming_tasks_.pop();
  }
}

// Unfortunately std::unique_lock does not support thread-safety annotations
void fexecutor::dispatcher_impl::purge_tasks_and_maybe_delete_self_locked(
    std::unique_lock<std::mutex> lock) FIT_NO_THREAD_SAFETY_ANALYSIS {
  ZX_DEBUG_ASSERT(lock.owns_lock());
  ZX_DEBUG_ASSERT(guarded_.was_shutdown_ || guarded_.loop_failure_);

  fasync::subtle::scheduler::task_queue tasks;
  accept_incoming_tasks_locked();
  tasks = guarded_.scheduler_.take_all_tasks();
  const bool can_delete_self = guarded_.was_shutdown_ && !guarded_.dispatch_pending_ &&
                               !guarded_.scheduler_.has_outstanding_tickets();

  lock.unlock();

  if (can_delete_self) {
    delete this;
  }
}

}  // namespace fasync
