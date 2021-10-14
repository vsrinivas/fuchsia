// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TASK_INTERNAL_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TASK_INTERNAL_H_

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>
#include <zircon/types.h>

namespace wlan::iwlwifi {

// Internal implementation of a task that runs on an async_dispatcher_t.
class TaskInternal : private async_task_t {
 public:
  using FuncType = void (*)(void* data);
  // Create the task.
  explicit TaskInternal(async_dispatcher_t* dispatcher, FuncType func, void* data);

  // Delete the task.  The task will be cancelled synchronously before deletion.
  ~TaskInternal();

  // Post the task, to be run after `delay`.
  zx_status_t Post(zx_duration_t delay);

  // Wait for the task to complete.  If the task has not been posted, return immediately.
  zx_status_t Wait();

  // Cancel the task, non-blocking.  May return:
  // * ZX_OK if the task was cancelled.
  // * ZX_ERR_NOT_FOUND if the task was not queued and thus not cancelled.
  // * Other errors in other error cases.
  zx_status_t Cancel();

  // Cancel the task, blocking.
  zx_status_t CancelSync();

 private:
  async_dispatcher_t* const dispatcher_ = nullptr;
  FuncType const func_ = nullptr;
  void* const data_ = nullptr;
  zx_futex_t state_ = 0;
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TASK_INTERNAL_H_
