// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>

#include <lib/async/cpp/time.h>
#include <zircon/assert.h>

#include <utility>

namespace async {
namespace internal {

struct RetainedTask : public async_task_t {
  RetainedTask(fit::closure handler, zx::time deadline)
      : async_task_t{{ASYNC_STATE_INIT}, &RetainedTask::Handler, deadline.get()},
        handler(static_cast<fit::closure&&>(handler)) {}

  fit::closure handler;

  static void Handler(async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status) {
    auto self = static_cast<RetainedTask*>(task);
    if (status == ZX_OK)
      self->handler();
    delete self;
  }
};

}  // namespace internal

zx_status_t PostTask(async_dispatcher_t* dispatcher, fit::closure handler) {
  return PostTaskForTime(dispatcher, static_cast<fit::closure&&>(handler), async::Now(dispatcher));
}

zx_status_t PostDelayedTask(async_dispatcher_t* dispatcher, fit::closure handler,
                            zx::duration delay) {
  return PostTaskForTime(dispatcher, static_cast<fit::closure&&>(handler),
                         async::Now(dispatcher) + delay);
}

zx_status_t PostTaskForTime(async_dispatcher_t* dispatcher, fit::closure handler,
                            zx::time deadline) {
  auto* task = new internal::RetainedTask(static_cast<fit::closure&&>(handler), deadline);
  zx_status_t status = async_post_task(dispatcher, task);
  if (status != ZX_OK)
    delete task;
  return status;
}

TaskBase::TaskBase(async_task_handler_t* handler)
    : task_{{ASYNC_STATE_INIT}, handler, ZX_TIME_INFINITE} {}

TaskBase::~TaskBase() {
  if (dispatcher_) {
    // Failure to cancel here may result in a dangling pointer...
    zx_status_t status = async_cancel_task(dispatcher_, &task_);
    ZX_ASSERT_MSG(status == ZX_OK, "status=%d", status);
  }
}

zx_status_t TaskBase::Post(async_dispatcher_t* dispatcher) {
  return PostForTime(dispatcher, async::Now(dispatcher));
}

zx_status_t TaskBase::PostDelayed(async_dispatcher_t* dispatcher, zx::duration delay) {
  return PostForTime(dispatcher, async::Now(dispatcher) + delay);
}

zx_status_t TaskBase::PostForTime(async_dispatcher_t* dispatcher, zx::time deadline) {
  if (dispatcher_)
    return ZX_ERR_ALREADY_EXISTS;

  dispatcher_ = dispatcher;
  task_.deadline = deadline.get();
  zx_status_t status = async_post_task(dispatcher, &task_);
  if (status != ZX_OK) {
    dispatcher_ = nullptr;
  }
  return status;
}

zx_status_t TaskBase::Cancel() {
  if (!dispatcher_)
    return ZX_ERR_NOT_FOUND;

  async_dispatcher_t* dispatcher = dispatcher_;
  dispatcher_ = nullptr;

  zx_status_t status = async_cancel_task(dispatcher, &task_);
  // |dispatcher| is required to be single-threaded, Cancel() is
  // only supposed to be called on |dispatcher|'s thread, and we
  // verified that the task was pending before calling
  // async_cancel_task(). Assuming that |dispatcher| does not yield
  // between removing the task and invoking the task's handler,
  // |task_| must have been pending with |dispatcher|.
  ZX_DEBUG_ASSERT(status != ZX_ERR_NOT_FOUND);
  return status;
}

Task::Task(Handler handler) : TaskBase(&Task::CallHandler), handler_(std::move(handler)) {}

Task::~Task() = default;

void Task::CallHandler(async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status) {
  auto self = Dispatch<Task>(task);
  self->handler_(dispatcher, self, status);
}

TaskClosure::TaskClosure(fit::closure handler)
    : TaskBase(&TaskClosure::CallHandler), handler_(std::move(handler)) {}

TaskClosure::~TaskClosure() = default;

void TaskClosure::CallHandler(async_dispatcher_t* dispatcher, async_task_t* task,
                              zx_status_t status) {
  auto self = Dispatch<TaskClosure>(task);  // must do this if status is not ok
  if (status == ZX_OK) {
    self->handler_();
  }
}

}  // namespace async
