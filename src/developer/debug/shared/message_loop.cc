// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/message_loop.h"

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <algorithm>

#include "src/lib/files/eintr_wrapper.h"
#include "src/lib/fxl/build_config.h"

namespace debug {

namespace {

thread_local MessageLoop* current_message_loop = nullptr;

}  // namespace

bool CreateLocalNonBlockingPipe(fbl::unique_fd* out_end, fbl::unique_fd* in_end) {
  int fds[2];

#if defined(OS_LINUX)
  if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
    return false;
#else
  if (pipe(fds) != 0)
    return false;

  for (auto i : {0, 1}) {
#if defined(OS_MACOSX)
    if (HANDLE_EINTR(fcntl(fds[i], F_SETFD, fcntl(fds[i], F_GETFD) | FD_CLOEXEC)) == -1)
      return false;
#endif
    if (HANDLE_EINTR(fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK)) == -1)
      return false;
  }
#endif

  out_end->reset(fds[0]);
  in_end->reset(fds[1]);
  return true;
}

fpromise::executor* MessageLoopContext::executor() const { return message_loop_; }

fpromise::suspended_task MessageLoopContext::suspend_task() {
  return message_loop_->SuspendCurrentTask();
}

MessageLoop::MessageLoop() : context_(this) {}

MessageLoop::~MessageLoop() {
  FX_DCHECK(Current() != this);  // Cleanup() should have been called.
}

bool MessageLoop::Init(std::string* error_message) {
  FX_DCHECK(!current_message_loop);
  current_message_loop = this;
  return true;
}

void MessageLoop::Cleanup() {
  FX_DCHECK(current_message_loop == this);

  // Clear all tasks because ~MessageLoop() might be called on another thread but some tasks might
  // hold thread_local resources.
  task_queue_.clear();
  timers_.clear();

  // Reset current_message_loop after tasks are cleared because some tasks may call StopWatching
  // during the destruction which checks current_message_loop.
  current_message_loop = nullptr;
}

// static
MessageLoop* MessageLoop::Current() { return current_message_loop; }

void MessageLoop::Run() {
  should_quit_ = false;
  RunImpl();
}

void MessageLoop::RunUntilNoTasks() {
  // Check if there are no tasks right now. If so, we exit immediately.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (task_queue_.empty())
      return;
  }

  should_quit_on_no_more_tasks_ = true;
  Run();
}

void MessageLoop::PostTask(FileLineFunction file_line, fit::function<void()> fn) {
  PostTaskInternal(std::move(file_line), std::move(fn));
}

void MessageLoop::PostTask(FileLineFunction file_line, fpromise::pending_task task) {
  PostTaskInternal(std::move(file_line), std::move(task));
}

void MessageLoop::RunTask(FileLineFunction file_line, fpromise::pending_task pending_task) {
  FX_DCHECK(pending_task);

  Task task(std::move(file_line), std::move(pending_task));
  RunOneTask(task);
}

void MessageLoop::PostTimer(FileLineFunction file_line, uint64_t delta_ms,
                            fit::function<void()> fn) {
  constexpr uint64_t kMsToNs = 1000000;

  bool needs_awaken;
  uint64_t expiry = delta_ms * kMsToNs + GetMonotonicNowNS();

  {
    std::lock_guard<std::mutex> guard(mutex_);
    needs_awaken = task_queue_.empty() && NextExpiryNS() > expiry;
    timers_.push_back({{std::move(file_line), std::move(fn)}, expiry});
    std::push_heap(timers_.begin(), timers_.end(), &CompareTimers);
  }
  if (needs_awaken)
    SetHasTasks();
}

void MessageLoop::schedule_task(fpromise::pending_task task) {
  PostTask(FROM_HERE, std::move(task));
}

fpromise::suspended_task::ticket MessageLoop::duplicate_ticket(
    fpromise::suspended_task::ticket ticket) {
  std::lock_guard<std::mutex> guard(mutex_);

  auto found = tickets_.find(ticket);
  FX_DCHECK(found != tickets_.end());

  FX_DCHECK(found->second.ref_count > 0);
  found->second.ref_count++;
  return ticket;
}

void MessageLoop::resolve_ticket(fpromise::suspended_task::ticket ticket, bool resume_task) {
  // Implementation note: The fpromise single_thread_executor has the behavior that resolving the
  // ticket moves the promise to the run queue, and then it's executed in order from there.
  //
  // However, this has the side effect of reordering the promise execution with respect to
  // non-promise-related tasks that are also executing on the message loop.
  //
  // As an example, consider attaching to a process which involves resolving a promise in the attach
  // reply message handler. There are non-promise-related messages in the message loop such as push
  // notifications about thread events from the remote debug agent. Requiring the resolution of the
  // promise be pushed to the back of the message queue will make it run after the processing of the
  // new thread messages and the replies would be executed in an order that doesn't make any sense.
  //
  // As a result, we run resolved promises synchronously when they're resolved.
  //
  // This has the disadvantage of potentially generating very deep stacks and one can construct
  // reentrant situations where this behavior might be surprising. But given the amount of
  // non-promise-related tasks our message loop currently runs and how most promises are only
  // resolved in response to IPC messages, the alternative is more surprising. If everything was a
  // promise, we could post it to the back of the task_queue_ with no problem (other than a slight
  // performance penalty by going through the loop again).

  Task task;                // The task (to run or delete outside of the lock).
  bool should_run = false;  // Whether to run the above task (otherwise just delete it).

  {
    std::lock_guard<std::mutex> guard(mutex_);

    FX_DCHECK(ticket != current_task_ticket_) << "Trying to resolve a task from within itself.";

    auto found = tickets_.find(ticket);
    FX_DCHECK(found != tickets_.end()) << "Bad ticket.";

    found->second.ref_count--;

    if (resume_task && !found->second.was_resumed) {
      // Task should be run (if was_resumed was already set, it was already moved to the run queue
      // so we don't have to do it again).
      should_run = true;

      // Mark as run. If the refcount isn't 0 yet this struct will still be around and we don't want
      // to run it again.
      found->second.was_resumed = true;
      task = Task(found->second.file_line, std::move(found->second.task));
    }

    if (found->second.ref_count == 0) {
      // Tickets are all closed. It it was resumed, the task will now be run queue, and if it wasn't
      // the task will be dropped with this operation.

      // Task could have already been moved out above.
      if (found->second.task) {
        // Free task outside lock, keep should_run false to avoid running.
        task = Task(FileLineFunction(), std::move(found->second.task));
      }
      tickets_.erase(found);
    }
  }
  if (should_run)
    RunOneTask(task);
}

uint64_t MessageLoop::DelayNS() const {
  // NextExpiry will return kMaxDelay if there are no timers queued.
  uint64_t expiry = NextExpiryNS();
  if (expiry == kMaxDelay) {
    return kMaxDelay;
  }

  // We check how much more time we need to wait.
  uint64_t now = GetMonotonicNowNS();
  if (expiry > now) {
    return expiry - now;
  }

  return 0;
}

uint64_t MessageLoop::NextExpiryNS() const {
  if (timers_.empty()) {
    return kMaxDelay;
  }

  return timers_[0].expiry;
}

fpromise::suspended_task MessageLoop::SuspendCurrentTask() {
  std::lock_guard<std::mutex> guard(mutex_);

  FX_DCHECK(current_task_is_promise_) << "Can only suspend when running a promise.";
  if (!current_task_ticket_) {
    // The current task has no ticket, make a new one.
    current_task_ticket_ = next_ticket_;
    next_ticket_++;

    tickets_.emplace(current_task_ticket_, TicketRecord());
  } else {
    duplicate_ticket(current_task_ticket_);
  }

  return fpromise::suspended_task(this, current_task_ticket_);
}

template <typename TaskType>
void MessageLoop::PostTaskInternal(FileLineFunction file_line, TaskType task) {
  bool needs_awaken;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    needs_awaken = task_queue_.empty();
    task_queue_.emplace_back(std::move(file_line), std::move(task));
  }
  if (needs_awaken)
    SetHasTasks();
}

void MessageLoop::RunOneTask(Task& task) {
  if (task.task_fn) {
    task.task_fn();
  } else if (task.pending) {
    // Run the fpromise::pending_task (generated by promises).

    // This code may be run nested via RunTask() so keep the old current task state so we can
    // restore it.
    bool old_task_is_promise = current_task_is_promise_;
    fpromise::suspended_task::ticket old_current_ticket = current_task_ticket_;

    current_task_is_promise_ = true;
    current_task_ticket_ = 0;

    bool finished = task.pending(context_);
    FX_DCHECK(!task.pending == finished) << "Finished state should be consistent.";
    (void)finished;

    if (current_task_ticket_) {
      // Task was suspended and a ticket was generated.
      //
      // This function locks again which is unfortunate. We could save this state and execute
      // this work after the mutex is locked again at the bottom of this loop, but that
      // complicates the execution flow.
      SaveTaskToTicket(current_task_ticket_, std::move(task.file_line), std::move(task.pending));
    }

    current_task_ticket_ = old_current_ticket;
    current_task_is_promise_ = old_task_is_promise;
  } else {
    FX_NOTREACHED();
  }
}

void MessageLoop::SaveTaskToTicket(fpromise::suspended_task::ticket ticket,
                                   FileLineFunction file_line, fpromise::pending_task task) {
  FX_DCHECK(task) << "The task should not be finished if we're saving it.";

  bool needs_awaken = false;

  std::lock_guard<std::mutex> guard(mutex_);

  {
    auto found = tickets_.find(ticket);
    FX_DCHECK(found != tickets_.end()) << "Ticket was invalid.";

    if (found->second.was_resumed) {
      // The ticket was suspended and then resumed from within the same run of the promise. It is
      // moved immediately to the runnable queue.
      needs_awaken = task_queue_.empty();
      task_queue_.emplace_back(std::move(file_line), std::move(task));
    } else if (found->second.ref_count != 0) {
      // Suspend tickets still out, keep suspended until marked resumed.
      found->second.task = std::move(task);
    }

    if (found->second.ref_count == 0) {
      // No refcount, can drop the ticket. The task could either be marked runnable and currently
      // scheduled to be run, or it could be dropped.
      tickets_.erase(found);
    }
  }

  if (needs_awaken)
    SetHasTasks();
}

void MessageLoop::QuitNow() { should_quit_ = true; }

bool MessageLoop::ProcessPendingTask() {
  // This function will be called with the mutex held.
  if (task_queue_.empty() && DelayNS() > 0) {
    if (should_quit_on_no_more_tasks_) {
      should_quit_on_no_more_tasks_ = false;
      QuitNow();
    }
    return false;
  }

  Task task;
  if (!task_queue_.empty()) {
    task = std::move(task_queue_.front());
    task_queue_.pop_front();
  } else {
    std::pop_heap(timers_.begin(), timers_.end(), &CompareTimers);
    task = std::move(timers_.back().task);
    timers_.pop_back();
  }

  mutex_.unlock();
  RunOneTask(task);
  mutex_.lock();

  return true;
}

MessageLoop::WatchHandle::WatchHandle() = default;
MessageLoop::WatchHandle::WatchHandle(MessageLoop* msg_loop, int id)
    : msg_loop_(msg_loop), id_(id) {}

MessageLoop::WatchHandle::WatchHandle(WatchHandle&& other)
    : msg_loop_(other.msg_loop_), id_(other.id_) {
  other.msg_loop_ = nullptr;
  other.id_ = 0;
}

MessageLoop::WatchHandle::~WatchHandle() { StopWatching(); }

void MessageLoop::WatchHandle::StopWatching() {
  if (watching())
    msg_loop_->StopWatching(id_);
  msg_loop_ = nullptr;
  id_ = 0;
}

MessageLoop::WatchHandle& MessageLoop::WatchHandle::operator=(WatchHandle&& other) {
  // Should never get into a self-assignment situation since this is not
  // copyable and every ID should be unique. Do allow self-assignment of
  // null ones though.
  FX_DCHECK(!watching() || (msg_loop_ != other.msg_loop_ || id_ != other.id_));
  if (watching())
    msg_loop_->StopWatching(id_);
  msg_loop_ = other.msg_loop_;
  id_ = other.id_;

  other.msg_loop_ = nullptr;
  other.id_ = 0;
  return *this;
}

}  // namespace debug
