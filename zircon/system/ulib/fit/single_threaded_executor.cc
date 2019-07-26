// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <condition_variable>
#include <mutex>

#include <lib/fit/single_threaded_executor.h>
#include <lib/fit/thread_safety.h>

namespace fit {

// The dispatcher runs tasks and provides the suspended task resolver.
//
// The lifetime of this object is somewhat complex since there are pointers
// to it from multiple sources which are released in different ways.
//
// - |single_threaded_executor| holds a pointer in |dispatcher_| which it releases
//   after calling |shutdown()| to inform the dispatcher of its own demise
// - |suspended_task| holds a pointer to the dispatcher's resolver
//   interface and the number of outstanding pointers corresponds to the
//   number of outstanding suspended task tickets tracked by |scheduler_|.
//
// The dispatcher deletes itself once all pointers have been released.
class single_threaded_executor::dispatcher_impl final : public suspended_task::resolver {
 public:
  dispatcher_impl();

  void shutdown();
  void schedule_task(pending_task task);
  void run(context_impl& context);
  suspended_task suspend_current_task();

  suspended_task::ticket duplicate_ticket(suspended_task::ticket ticket) override;
  void resolve_ticket(suspended_task::ticket ticket, bool resume_task) override;

 private:
  ~dispatcher_impl() override;

  void wait_for_runnable_tasks(fit::subtle::scheduler::task_queue* out_tasks);
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

single_threaded_executor::single_threaded_executor()
    : context_(this), dispatcher_(new dispatcher_impl()) {}

single_threaded_executor::~single_threaded_executor() { dispatcher_->shutdown(); }

void single_threaded_executor::schedule_task(pending_task task) {
  assert(task);
  dispatcher_->schedule_task(std::move(task));
}

void single_threaded_executor::run() { dispatcher_->run(context_); }

single_threaded_executor::context_impl::context_impl(single_threaded_executor* executor)
    : executor_(executor) {}

single_threaded_executor::context_impl::~context_impl() = default;

single_threaded_executor* single_threaded_executor::context_impl::executor() const {
  return executor_;
}

suspended_task single_threaded_executor::context_impl::suspend_task() {
  return executor_->dispatcher_->suspend_current_task();
}

single_threaded_executor::dispatcher_impl::dispatcher_impl() = default;

single_threaded_executor::dispatcher_impl::~dispatcher_impl() {
  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  assert(guarded_.was_shutdown_);
  assert(!guarded_.scheduler_.has_runnable_tasks());
  assert(!guarded_.scheduler_.has_suspended_tasks());
  assert(!guarded_.scheduler_.has_outstanding_tickets());
}

void single_threaded_executor::dispatcher_impl::shutdown() {
  fit::subtle::scheduler::task_queue tasks;  // drop outside of the lock
  {
    std::lock_guard<std::mutex> lock(guarded_.mutex_);
    assert(!guarded_.was_shutdown_);
    guarded_.was_shutdown_ = true;
    guarded_.scheduler_.take_all_tasks(&tasks);
    if (guarded_.scheduler_.has_outstanding_tickets()) {
      return;  // can't delete self yet
    }
  }

  // Must destroy self outside of the lock.
  delete this;
}

void single_threaded_executor::dispatcher_impl::schedule_task(pending_task task) {
  {
    std::lock_guard<std::mutex> lock(guarded_.mutex_);
    assert(!guarded_.was_shutdown_);
    guarded_.scheduler_.schedule_task(std::move(task));
    if (!guarded_.need_wake_) {
      return;  // don't need to wake
    }
    guarded_.need_wake_ = false;
  }

  // It is more efficient to notify outside the lock.
  wake_.notify_one();
}

void single_threaded_executor::dispatcher_impl::run(context_impl& context) {
  fit::subtle::scheduler::task_queue tasks;
  for (;;) {
    wait_for_runnable_tasks(&tasks);
    if (tasks.empty()) {
      return;  // all done!
    }

    do {
      run_task(&tasks.front(), context);
      tasks.pop();  // the task may be destroyed here if it was not suspended
    } while (!tasks.empty());
  }
}

// Must only be called while |run_task()| is running a task.
// This happens when the task's continuation calls |context::suspend_task()|
// upon the context it received as an argument.
suspended_task single_threaded_executor::dispatcher_impl::suspend_current_task() {
  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  assert(!guarded_.was_shutdown_);
  if (current_task_ticket_ == 0) {
    current_task_ticket_ = guarded_.scheduler_.obtain_ticket(2 /*initial_refs*/);
  } else {
    guarded_.scheduler_.duplicate_ticket(current_task_ticket_);
  }
  return suspended_task(this, current_task_ticket_);
}

// Unfortunately std::unique_lock does not support thread-safety annotations
void single_threaded_executor::dispatcher_impl::wait_for_runnable_tasks(
    fit::subtle::scheduler::task_queue* out_tasks) FIT_NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock<std::mutex> lock(guarded_.mutex_);
  for (;;) {
    assert(!guarded_.was_shutdown_);
    guarded_.scheduler_.take_runnable_tasks(out_tasks);
    if (!out_tasks->empty()) {
      return;  // got some tasks
    }
    if (!guarded_.scheduler_.has_suspended_tasks()) {
      return;  // all done!
    }
    guarded_.need_wake_ = true;
    wake_.wait(lock);
    guarded_.need_wake_ = false;
  }
}

void single_threaded_executor::dispatcher_impl::run_task(pending_task* task, context& context) {
  assert(current_task_ticket_ == 0);
  const bool finished = (*task)(context);
  assert(!*task == finished);
  (void)finished;
  if (current_task_ticket_ == 0) {
    return;  // task was not suspended, no ticket was produced
  }

  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  assert(!guarded_.was_shutdown_);
  guarded_.scheduler_.finalize_ticket(current_task_ticket_, task);
  current_task_ticket_ = 0;
}

suspended_task::ticket single_threaded_executor::dispatcher_impl::duplicate_ticket(
    suspended_task::ticket ticket) {
  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  guarded_.scheduler_.duplicate_ticket(ticket);
  return ticket;
}

void single_threaded_executor::dispatcher_impl::resolve_ticket(suspended_task::ticket ticket,
                                                               bool resume_task) {
  pending_task abandoned_task;  // drop outside of the lock
  bool do_wake = false;
  {
    std::lock_guard<std::mutex> lock(guarded_.mutex_);
    if (resume_task) {
      guarded_.scheduler_.resume_task_with_ticket(ticket);
    } else {
      abandoned_task = guarded_.scheduler_.release_ticket(ticket);
    }
    if (guarded_.was_shutdown_) {
      assert(!guarded_.need_wake_);
      if (guarded_.scheduler_.has_outstanding_tickets()) {
        return;  // can't shutdown yet
      }
    } else if (guarded_.need_wake_ && (guarded_.scheduler_.has_runnable_tasks() ||
                                       !guarded_.scheduler_.has_suspended_tasks())) {
      guarded_.need_wake_ = false;
      do_wake = true;
    } else {
      return;  // nothing else to do
    }
  }

  // Must do this outside of the lock.
  if (do_wake) {
    wake_.notify_one();
  } else {
    delete this;
  }
}

}  // namespace fit
