// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Can't compile this for Zircon userspace yet since libstdc++ isn't available.
#ifndef FIT_NO_STD_FOR_ZIRCON_USERSPACE

#include <lib/fit/sequential_executor.h>

namespace fit {

sequential_executor::sequential_executor()
    : context_(this), dispatcher_(new dispatcher_impl()) {}

sequential_executor::~sequential_executor() {
    dispatcher_->shutdown();
}

void sequential_executor::schedule_task(pending_task task) {
    assert(task);
    dispatcher_->schedule_task(std::move(task));
}

void sequential_executor::run() {
    dispatcher_->run(context_);
}

sequential_executor::context_impl::context_impl(sequential_executor* executor)
    : executor_(executor) {}

sequential_executor::context_impl::~context_impl() = default;

sequential_executor* sequential_executor::context_impl::executor() const {
    return executor_;
}

suspended_task sequential_executor::context_impl::suspend_task() {
    return executor_->dispatcher_->suspend_current_task();
}

sequential_executor::dispatcher_impl::dispatcher_impl() = default;

sequential_executor::dispatcher_impl::~dispatcher_impl() {
    std::lock_guard<std::mutex> lock(guarded_.mutex_);
    assert(guarded_.was_shutdown_);
    assert(!guarded_.scheduler_.has_runnable_tasks());
    assert(!guarded_.scheduler_.has_suspended_tasks());
    assert(!guarded_.scheduler_.has_outstanding_tickets());
}

void sequential_executor::dispatcher_impl::shutdown() {
    fit::subtle::scheduler::task_queue tasks; // drop outside of the lock
    {
        std::lock_guard<std::mutex> lock(guarded_.mutex_);
        assert(!guarded_.was_shutdown_);
        guarded_.was_shutdown_ = true;
        guarded_.scheduler_.take_all_tasks(&tasks);
        if (guarded_.scheduler_.has_outstanding_tickets()) {
            return; // can't delete self yet
        }
    }

    // Must destroy self outside of the lock.
    delete this;
}

void sequential_executor::dispatcher_impl::schedule_task(pending_task task) {
    {
        std::lock_guard<std::mutex> lock(guarded_.mutex_);
        assert(!guarded_.was_shutdown_);
        guarded_.scheduler_.schedule_task(std::move(task));
        if (!guarded_.need_wake_) {
            return; // don't need to wake
        }
        guarded_.need_wake_ = false;
    }

    // It is more efficient to notify outside the lock.
    wake_.notify_one();
}

void sequential_executor::dispatcher_impl::run(context_impl& context) {
    fit::subtle::scheduler::task_queue tasks;
    for (;;) {
        wait_for_runnable_tasks(&tasks);
        if (tasks.empty()) {
            return; // all done!
        }

        do {
            run_task(&tasks.front(), context);
            tasks.pop(); // the task may be destroyed here if it was not suspended
        } while (!tasks.empty());
    }
}

// Must only be called while |run_task()| is running a task.
// This happens when the task's continuation calls |context::suspend_task()|
// upon the context it received as an argument.
suspended_task sequential_executor::dispatcher_impl::suspend_current_task() {
    std::lock_guard<std::mutex> lock(guarded_.mutex_);
    assert(!guarded_.was_shutdown_);
    if (current_task_ticket_ == 0) {
        current_task_ticket_ = guarded_.scheduler_.obtain_ticket(
            2 /*initial_refs*/);
    } else {
        guarded_.scheduler_.duplicate_ticket(current_task_ticket_);
    }
    return suspended_task(this, current_task_ticket_);
}

void sequential_executor::dispatcher_impl::wait_for_runnable_tasks(
    fit::subtle::scheduler::task_queue* out_tasks) {
    std::unique_lock<std::mutex> lock(guarded_.mutex_);
    for (;;) {
        assert(!guarded_.was_shutdown_);
        guarded_.scheduler_.take_runnable_tasks(out_tasks);
        if (!out_tasks->empty()) {
            return; // got some tasks
        }
        if (!guarded_.scheduler_.has_suspended_tasks()) {
            return; // all done!
        }
        guarded_.need_wake_ = true;
        wake_.wait(lock);
        guarded_.need_wake_ = false;
    }
}

void sequential_executor::dispatcher_impl::run_task(pending_task* task,
                                                    context& context) {
    assert(current_task_ticket_ == 0);
    const bool finished = (*task)(context);
    assert(!*task == finished);
    if (current_task_ticket_ == 0) {
        return; // task was not suspended, no ticket was produced
    }

    std::lock_guard<std::mutex> lock(guarded_.mutex_);
    assert(!guarded_.was_shutdown_);
    guarded_.scheduler_.finalize_ticket(current_task_ticket_, task);
    current_task_ticket_ = 0;
}

suspended_task::ticket sequential_executor::dispatcher_impl::duplicate_ticket(
    suspended_task::ticket ticket) {
    std::lock_guard<std::mutex> lock(guarded_.mutex_);
    guarded_.scheduler_.duplicate_ticket(ticket);
    return ticket;
}

void sequential_executor::dispatcher_impl::resolve_ticket(
    suspended_task::ticket ticket, bool resume_task) {
    pending_task abandoned_task; // drop outside of the lock
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
                return; // can't shutdown yet
            }
        } else if (guarded_.need_wake_ &&
                   (guarded_.scheduler_.has_runnable_tasks() ||
                    !guarded_.scheduler_.has_suspended_tasks())) {
            guarded_.need_wake_ = false;
            do_wake = true;
        } else {
            return; // nothing else to do
        }
    }

    // Must do this outside of the lock.
    if (do_wake) {
        wake_.notify_one();
    } else {
        delete this;
    }
}

} // namespace fit

#endif // FIT_NO_STD_FOR_ZIRCON_USERSPACE
