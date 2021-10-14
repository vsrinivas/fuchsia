// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/task.h"

#include <lib/async/dispatcher.h>

#include <memory>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/task-internal.h"

struct iwl_task : public wlan::iwlwifi::TaskInternal {
 public:
  explicit iwl_task(async_dispatcher_t* dispatcher, iwl_task_func func, void* data)
      : TaskInternal(dispatcher, func, data) {}
};

zx_status_t iwl_task_create(struct device* dev, iwl_task_func func, void* data,
                            struct iwl_task** out_task) {
  *out_task = new iwl_task(dev->task_dispatcher, func, data);
  return ZX_OK;
}

zx_status_t iwl_task_post(struct iwl_task* task, zx_duration_t delay) { return task->Post(delay); }

zx_status_t iwl_task_wait(struct iwl_task* task) { return task->Wait(); }

zx_status_t iwl_task_cancel(struct iwl_task* task) { return task->Cancel(); }

zx_status_t iwl_task_cancel_sync(struct iwl_task* task) { return task->CancelSync(); }

void iwl_task_release_sync(struct iwl_task* task) { delete task; }
