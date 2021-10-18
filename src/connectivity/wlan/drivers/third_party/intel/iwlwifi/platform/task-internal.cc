// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/task-internal.h"

#include <lib/async/time.h>
#include <lib/stdcompat/atomic.h>
#include <zircon/assert.h>

#include <limits>

namespace wlan::iwlwifi {
namespace {

enum TaskState : zx_futex_t {
  kInvalid = 0,
  kIdle = 1,
  kQueued = 2,
  kExecuting = 3,
};

}  // namespace

TaskInternal::TaskInternal(async_dispatcher_t* dispatcher, FuncType func, void* data)
    : async_task_t{}, dispatcher_(dispatcher), func_(func), data_(data), state_(TaskState::kIdle) {
  this->handler = [](async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status) {
    auto task_internal = static_cast<TaskInternal*>(task);
    cpp20::atomic_ref<zx_futex_t> state_ref(task_internal->state_);
    ZX_DEBUG_ASSERT(state_ref.load() == TaskState::kQueued);

    if (status == ZX_OK) {
      state_ref.store(TaskState::kExecuting, std::memory_order_release);
      (*task_internal->func_)(task_internal->data_);
    }

    state_ref.store(TaskState::kIdle, std::memory_order_release);
    zx_futex_wake(&task_internal->state_, std::numeric_limits<uint32_t>::max());
  };
}

TaskInternal::~TaskInternal() {
  CancelSync();
  ZX_DEBUG_ASSERT(cpp20::atomic_ref<zx_futex_t>(state_).load() == TaskState::kIdle);
}

zx_status_t TaskInternal::Post(zx_duration_t delay) {
  zx_status_t status = ZX_OK;

  // The async_dpsatcher interface does not allow tasks to be multiply posted.
  if ((status = CancelSync()) != ZX_OK) {
    if (status != ZX_ERR_NOT_FOUND) {
      return status;
    }
  }

  zx_futex_t expected = TaskState::kIdle;
  zx_futex_t desired = TaskState::kQueued;
  cpp20::atomic_ref<zx_futex_t> state_ref(state_);
  if (!state_ref.compare_exchange_strong(expected, desired, std::memory_order_acq_rel)) {
    // Post() is being called simultaneously from multiple threads, so we just early-return the
    // calls after the first.
    return ZX_OK;
  }

  // Post the task.
  this->deadline = async_now(dispatcher_) + delay;
  if ((status = async_post_task(dispatcher_, this)) != ZX_OK) {
    state_ref.store(TaskState::kIdle, std::memory_order_release);
    zx_futex_wake(&state_, std::numeric_limits<uint32_t>::max());
    return status;
  }

  return ZX_OK;
}

zx_status_t TaskInternal::Wait() {
  zx_status_t status = ZX_OK;
  cpp20::atomic_ref<zx_futex_t> state_ref(state_);
  zx_futex_t value = state_ref.load(std::memory_order_acquire);
  while (value != TaskState::kIdle) {
    if ((status = zx_futex_wait(&state_, value, ZX_HANDLE_INVALID, ZX_TIME_INFINITE)) != ZX_OK) {
      if (status != ZX_ERR_BAD_STATE) {
        return status;
      }

      // ZX_ERR_BAD_STATE means that `state_` has already changed to be different than `value`,
      // which is not an error condition here.
    }
    value = state_ref.load(std::memory_order_acquire);
  }

  return ZX_OK;
}

zx_status_t TaskInternal::Cancel() {
  zx_status_t status = async_cancel_task(dispatcher_, this);
  if (status != ZX_OK) {
    return status;
  }

  cpp20::atomic_ref<zx_futex_t> state_ref(state_);
  ZX_DEBUG_ASSERT(state_ref.load() == TaskState::kQueued);
  state_ref.store(TaskState::kIdle, std::memory_order_release);
  zx_futex_wake(&state_, std::numeric_limits<uint32_t>::max());

  return ZX_OK;
}

zx_status_t TaskInternal::CancelSync() {
  // Cancel the task.
  zx_status_t status = ZX_OK;
  if ((status = Cancel()) != ZX_OK) {
    return status;
  }

  // Now wait for completion.
  return Wait();
}

}  // namespace wlan::iwlwifi
