// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cancelable_task.h"

#include <async/default.h>
#include <zircon/status.h>
#include <zx/time.h>

#include "lib/fxl/logging.h"

namespace btlib {
namespace common {

CancelableTask::CancelableTask() : posted_(false) {}

CancelableTask::~CancelableTask() {
  Cancel();
}

void CancelableTask::Cancel() {
  if (!posted_)
    return;

  zx_status_t status = task_.Cancel(async_get_default());
  if (status != ZX_OK) {
    FXL_VLOG(1) << "common: CancelableTask: failed to cancel task: "
                << zx_status_get_string(status);
  }

  posted_ = false;
}

bool CancelableTask::Post(fxl::Closure task, zx::duration nanoseconds) {
  if (posted_)
    return false;

  task_.set_deadline(zx::deadline_after(nanoseconds).get());
  task_.set_handler(
      [this, task = std::move(task)](async_t*, zx_status_t status) {
        posted_ = false;
        if (status == ZX_OK) {
          task();
        }
        return ASYNC_TASK_FINISHED;
      });

  zx_status_t status = task_.Post(async_get_default());
  if (status != ZX_OK) {
    FXL_VLOG(1) << "common: CancelableTask: failed to post task: "
                << zx_status_get_string(status);
    return false;
  }

  posted_ = true;
  return true;
}

}  // namespace common
}  // namespace btlib
