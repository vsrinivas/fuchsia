// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TASK_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TASK_H_

// This file defines the work queue interface, equivalent to schedule_work() and friends in Linux.
// These are tasks that are run in Linux in process context; as such these tasks are allowed to
// sleep, block, etc.

#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

struct iwl_task;
typedef void (*iwl_task_func)(void* data);

// Create a task.
zx_status_t iwl_task_create(struct device* dev, iwl_task_func func, void* data,
                            struct iwl_task** out_task);

// Post the task to the work queue dispatcher, to be run after `delay`.  If the task is already
// posted, it will be cancelled first.
zx_status_t iwl_task_post(struct iwl_task* task, zx_duration_t delay);

// Wait for the task to complete.  If the task has not been posted, it will return immediately.
zx_status_t iwl_task_wait(struct iwl_task* task);

// Cancel the task on the work queue.  This call does not block: once this call returns, the task is
// is no longer queued to the work queue; it has either been dequeued and dispatched (but may be
// currently executing), or cancelled.  May return:
// * ZX_OK if the task was cancelled.
// * ZX_ERR_NOT_FOUND if the task was not queued and thus not cancelled.
// * Other errors in other error cases.
zx_status_t iwl_task_cancel(struct iwl_task* task);

// Cancel the task on the work queue, synchronously.  This call blocks and guarantees that the task
// is not queued to the work queue and is no longer running.
// * ZX_OK if the task was cancelled.
// * ZX_ERR_NOT_FOUND if the task was not queued and thus not cancelled.
// * Other errors in other error cases.
zx_status_t iwl_task_cancel_sync(struct iwl_task* task);

// Release (and deallocate) the task, synchronously.  If the task is running, it will be
// synchronously.
void iwl_task_release_sync(struct iwl_task* task);

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_TASK_H_
