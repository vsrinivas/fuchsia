// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/single_threaded_executor.h>
#include <lib/fit/thread_safety.h>
#include <lib/stdcompat/optional.h>

#include <condition_variable>
#include <mutex>

namespace fasync {

// The dispatcher runs tasks and provides the suspended task resolver.
//
// The lifetime of this object is somewhat complex since there are pointers to it from multiple
// sources which are released in different ways.
//
// - |single_threaded_executor| holds a pointer in |dispatcher_| which it releases after calling
//   |shutdown_and_maybe_destroy()| to inform the dispatcher of its own demise. The dispatcher will
//   delete itself in this case if it does not have outstanding tickets.
// - |suspended_task| holds a pointer to the dispatcher's resolver interface and the number of
//   outstanding pointers corresponds to the number of outstanding suspended task tickets
//   tracked by |scheduler_|. The dispatcher will delete itself in this case if it has been shutdown
//   (the |fasync::single_threaded_executor| was destroyed) and there are no outstanding tickets on
//   a call to |dispatcher_impl::resolve_ticket()|.
//
// The dispatcher deletes itself once all pointers have been released.
class single_threaded_executor::dispatcher_impl final : public suspended_task::resolver {
 public:
  dispatcher_impl() = default;

  void shutdown_and_maybe_destroy();
  void schedule(pending_task&& task);
  void run(context_impl& context);
  suspended_task suspend_current_task();

  suspended_task::ticket duplicate_ticket(suspended_task::ticket ticket) override;
  void resolve_ticket(suspended_task::ticket ticket, bool resume_task) override;

 private:
  // Need one ref for the dispatcher and one to return to the client.
  static constexpr ::fasync::subtle::scheduler::ref_count_type initial_refs = 2;

  ~dispatcher_impl() override {
    std::lock_guard<std::mutex> lock(guarded_.mutex_);
    assert(guarded_.was_shutdown_);
    assert(!guarded_.scheduler_.has_runnable_tasks());
    assert(!guarded_.scheduler_.has_suspended_tasks());
    assert(!guarded_.scheduler_.has_outstanding_tickets());
  }

  ::fasync::subtle::scheduler::task_queue wait_for_runnable_tasks();
  void run_task(pending_task& task, context& context);

  suspended_task::ticket current_task_ticket_ = 0;
  std::condition_variable wake_;

  // A bunch of state that is guarded by a mutex.
  struct {
    std::mutex mutex_;
    bool was_shutdown_ FIT_GUARDED(mutex_) = false;
    bool need_wake_ FIT_GUARDED(mutex_) = false;
    ::fasync::subtle::scheduler scheduler_ FIT_GUARDED(mutex_);
    ::fasync::subtle::scheduler::task_queue tasks_to_destroy_ FIT_GUARDED(mutex_);
  } guarded_;
};

single_threaded_executor::single_threaded_executor()
    : context_(*this), dispatcher_(new dispatcher_impl()) {}

single_threaded_executor::~single_threaded_executor() { dispatcher_->shutdown_and_maybe_destroy(); }

void single_threaded_executor::schedule(pending_task&& task) {
  dispatcher_->schedule(std::move(task));
}

void single_threaded_executor::run() { dispatcher_->run(context_); }

void single_threaded_executor::dispatcher_impl::shutdown_and_maybe_destroy() {
  ::fasync::subtle::scheduler::task_queue tasks;  // Drop outside of the lock.
  {
    std::lock_guard<std::mutex> lock(guarded_.mutex_);
    assert(!guarded_.was_shutdown_);
    guarded_.was_shutdown_ = true;
    tasks = guarded_.scheduler_.take_all_tasks();
    if (guarded_.scheduler_.has_outstanding_tickets()) {
      return;  // Can't delete self yet.
    }
  }

  // Must destroy self outside of the lock.
  // See comment on |dispatcher_impl| for how the lifetime of this object is managed.
  delete this;
}

void single_threaded_executor::dispatcher_impl::schedule(pending_task&& task) {
  bool need_wake;
  {
    std::lock_guard<std::mutex> lock(guarded_.mutex_);
    assert(!guarded_.was_shutdown_);
    guarded_.scheduler_.schedule(std::move(task));
    need_wake = guarded_.need_wake_;
    guarded_.need_wake_ = false;
  }

  // Release the lock before notifying to avoid unnecessary contention.
  if (need_wake) {
    wake_.notify_one();
  }
}

void single_threaded_executor::dispatcher_impl::run(context_impl& context) {
  ::fasync::subtle::scheduler::task_queue tasks;
  while (true) {
    {
      std::lock_guard<std::mutex> lock(guarded_.mutex_);
      guarded_.tasks_to_destroy_ = {};
    }
    tasks = wait_for_runnable_tasks();
    if (tasks.empty()) {
      return;  // All done!
    }
    do {
      run_task(tasks.front(), context);
      tasks.pop();  // The task may be destroyed here if it was not suspended.
    } while (!tasks.empty());
  }
}

// Must only be called while |run_task()| is running a task. This happens when the task's
// continuation calls |context::suspend_task()| upon the context it received as an argument.
suspended_task single_threaded_executor::dispatcher_impl::suspend_current_task() {
  std::lock_guard<std::mutex> lock(guarded_.mutex_);
  assert(!guarded_.was_shutdown_);
  if (current_task_ticket_ == 0) {
    current_task_ticket_ =
        guarded_.scheduler_.obtain_ticket(single_threaded_executor::dispatcher_impl::initial_refs);
  } else {
    guarded_.scheduler_.duplicate_ticket(current_task_ticket_);
  }
  return suspended_task(*this, current_task_ticket_);
}

// Unfortunately std::unique_lock does not support thread-safety annotations.
::fasync::subtle::scheduler::task_queue
single_threaded_executor::dispatcher_impl::wait_for_runnable_tasks() {
  ::fasync::subtle::scheduler::task_queue tasks;
  std::unique_lock<std::mutex> lock(guarded_.mutex_);
  []() FIT_THREAD_ANNOTATION(__assert_capability__(guarded_.mutex_)) {}();
  while (true) {
    assert(!guarded_.was_shutdown_);
    tasks = guarded_.scheduler_.take_runnable_tasks();
    if (!tasks.empty()) {
      return tasks;  // Got some tasks.
    }
    if (!guarded_.scheduler_.has_suspended_tasks()) {
      return tasks;  // All done!
    }
    guarded_.need_wake_ = true;
    wake_.wait(lock);
    guarded_.need_wake_ = false;
  }
}

void single_threaded_executor::dispatcher_impl::run_task(pending_task& task, context& context) {
  assert(current_task_ticket_ == 0);
  task(context);
  if (current_task_ticket_ == 0) {
    return;  // Task was not suspended, no ticket was produced.
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
  cpp17::optional<pending_task> abandoned_task;  // Drop outside of the lock.
  bool do_wake = false;
  {
    std::lock_guard<std::mutex> lock(guarded_.mutex_);
    if (resume_task) {
      guarded_.scheduler_.resume_task_with_ticket(ticket);
    } else {
      abandoned_task = guarded_.scheduler_.release_ticket(ticket);
      if (abandoned_task.has_value()) {
        guarded_.tasks_to_destroy_.emplace(std::move(*abandoned_task));
      }
    }
    if (guarded_.was_shutdown_) {
      assert(!guarded_.need_wake_);
      if (guarded_.scheduler_.has_outstanding_tickets()) {
        return;  // Can't destroy yet.
      }
    } else if (guarded_.need_wake_ && (guarded_.scheduler_.has_runnable_tasks() ||
                                       !guarded_.scheduler_.has_suspended_tasks())) {
      guarded_.need_wake_ = false;
      do_wake = true;
    } else {
      return;  // Nothing else to do.
    }
  }

  // Must do this outside of the lock.
  if (do_wake) {
    wake_.notify_one();
  } else {
    // See comment on |dispatcher_impl| for how the lifetime of this object is managed.
    delete this;
  }
}

suspended_task single_threaded_executor::context_impl::suspend_task() {
  return executor_.dispatcher_->suspend_current_task();
}

}  // namespace fasync
