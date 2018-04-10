// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <lib/async/task.h>
#include <lib/zx/time.h>

namespace async {

// Posts a task to invoke |handler| with a deadline of now.
//
// The handler will not run if the dispatcher shuts down before it comes due.
//
// Returns |ZX_OK| if the task was successfully posted.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
zx_status_t PostTask(async_t* async, fbl::Closure handler);

// Posts a task to invoke |handler| with a deadline expressed as a |delay| from now.
//
// The handler will not run if the dispatcher shuts down before it comes due.
//
// Returns |ZX_OK| if the task was successfully posted.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
zx_status_t PostDelayedTask(async_t* async, fbl::Closure handler, zx::duration delay);

// Posts a task to invoke |handler| with the specified |deadline|.
//
// The handler will not run if the dispatcher shuts down before it comes due.
//
// Returns |ZX_OK| if the task was successfully posted.
// Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
// Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
zx_status_t PostTaskForTime(async_t* async, fbl::Closure handler, zx::time deadline);

// Holds context for a task and its handler, with RAII semantics.
// Automatically cancels the task when it goes out of scope.
//
// After successfully posting the task, the client is responsible for retaining
// the structure in memory (and unmodified) until the task's handler runs, the task
// is successfully canceled, or the dispatcher shuts down.  Thereafter, the task
// may be posted again or destroyed.
//
// This class must only be used with single-threaded asynchronous dispatchers
// and must only be accessed on the dispatch thread since it lacks internal
// synchronization of its state.
//
// Concrete implementations: |async::Task|, |async::TaskMethod|,
//   |async::TaskClosure|, |async::TaskClosureMethod|.
// Please do not create subclasses of TaskBase outside of this library.
class TaskBase {
protected:
    explicit TaskBase(async_task_handler_t* handler);
    ~TaskBase();

    TaskBase(const TaskBase&) = delete;
    TaskBase(TaskBase&&) = delete;
    TaskBase& operator=(const TaskBase&) = delete;
    TaskBase& operator=(TaskBase&&) = delete;

public:
    // Returns true if the task has been posted and has not yet executed or been canceled.
    bool is_pending() const { return async_ != nullptr; }

    // The last deadline with which the task was posted, or |zx::time::infinite()|
    // if it has never been posted.
    zx::time last_deadline() const { return zx::time(task_.deadline); }

    // Posts a task to invoke the handler with a deadline of now.
    //
    // Returns |ZX_OK| if the task was successfully posted.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down or if the
    // task is already pending.
    // Returns |ZX_ERR_ALREADY_EXISTS| if the task is already pending.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t Post(async_t* async);

    // Posts a task to invoke the handler with a deadline expressed as a |delay| from now.
    //
    // Returns |ZX_OK| if the task was successfully posted.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down or if the
    // task is already pending.
    // Returns |ZX_ERR_ALREADY_EXISTS| if the task is already pending.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t PostDelayed(async_t* async, zx::duration delay);

    // Posts a task to invoke the handler with the specified |deadline|.
    //
    // The |deadline| must be expressed in the time base used by the asynchronous
    // dispatcher (usually |ZX_CLOCK_MONOTONIC| except in unit tests).
    // See |async_now()| for details.
    //
    // Returns |ZX_OK| if the task was successfully posted.
    // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down or if the
    // task is already pending.
    // Returns |ZX_ERR_ALREADY_EXISTS| if the task is already pending.
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t PostForTime(async_t* async, zx::time deadline);

    // Cancels the task.
    //
    // If successful, the task's handler will not run.
    //
    // Returns |ZX_OK| if the task was pending and it has been successfully
    // canceled; its handler will not run again and can be released immediately.
    // Returns |ZX_ERR_NOT_FOUND| if task was not pending either because its
    // handler already ran, the task had not been posted, or the task has
    // already been dequeued and is pending execution (perhaps on another thread).
    // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
    zx_status_t Cancel();

protected:
    template <typename T>
    static T* Dispatch(async_task_t* task) {
        static_assert(offsetof(TaskBase, task_) == 0, "");
        auto self = reinterpret_cast<TaskBase*>(task);
        self->async_ = nullptr;
        return static_cast<T*>(self);
    }

private:
    async_task_t task_;
    async_t* async_ = nullptr;
};

// A task whose handler is bound to a |async::Task::Handler| function.
//
// Prefer using |async::TaskMethod| instead for binding to a fixed class member
// function since it is more efficient to dispatch.
class Task final : public TaskBase {
public:
    // Handles execution of a posted task.
    //
    // The |status| is |ZX_OK| if the task's deadline elapsed and the task should run.
    // The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down before
    // the task's handler ran or the task was canceled.
    using Handler = fbl::Function<void(async_t* async, async::Task* task, zx_status_t status)>;

    explicit Task(Handler handler = nullptr);
    ~Task();

    void set_handler(Handler handler) { handler_ = fbl::move(handler); }
    bool has_handler() const { return !!handler_; }

private:
    static void CallHandler(async_t* async, async_task_t* task, zx_status_t status);

    Handler handler_;
};

// A task whose handler is bound to a fixed class member function.
//
// Usage:
//
// class Foo {
//     void Handle(async_t* async, async::TaskBase* task, zx_status_t status) { ... }
//     async::TaskMethod<Foo, &Foo::Handle> task_{this};
// };
template <class Class,
          void (Class::*method)(async_t* async, async::TaskBase* task, zx_status_t status)>
class TaskMethod final : public TaskBase {
public:
    explicit TaskMethod(Class* instance)
        : TaskBase(&TaskMethod::CallHandler), instance_(instance) {}
    ~TaskMethod() = default;

private:
    static void CallHandler(async_t* async, async_task_t* task, zx_status_t status) {
        auto self = Dispatch<TaskMethod>(task);
        (self->instance_->*method)(async, self, status);
    }

    Class* const instance_;
};

// A task whose handler is bound to a |fbl::Closure| function with no arguments.
// The closure is not invoked when errors occur since it doesn't have a |zx_status_t|
// argument.
//
// Prefer using |async::TaskClosureMethod| instead for binding to a fixed class member
// function since it is more efficient to dispatch.
class TaskClosure final : public TaskBase {
public:
    explicit TaskClosure(fbl::Closure handler = nullptr);
    ~TaskClosure();

    void set_handler(fbl::Closure handler) { handler_ = fbl::move(handler); }
    bool has_handler() const { return !!handler_; }

private:
    static void CallHandler(async_t* async, async_task_t* task, zx_status_t status);

    fbl::Closure handler_;
};

// A task whose handler is bound to a fixed class member function with no arguments.
// The closure is not invoked when errors occur since it doesn't have a |zx_status_t|
// argument.
//
// Usage:
//
// class Foo {
//     void Handle() { ... }
//     async::TaskClosureMethod<Foo, &Foo::Handle> trap_{this};
// };
template <class Class,
          void (Class::*method)()>
class TaskClosureMethod final : public TaskBase {
public:
    explicit TaskClosureMethod(Class* instance)
        : TaskBase(&TaskClosureMethod::CallHandler), instance_(instance) {}
    ~TaskClosureMethod() = default;

private:
    static void CallHandler(async_t* async, async_task_t* task, zx_status_t status) {
        auto self = Dispatch<TaskClosureMethod>(task); // must do this if status is not ok
        if (status == ZX_OK) {
            (self->instance_->*method)();
        }
    }

    Class* const instance_;
};

} // namespace async
