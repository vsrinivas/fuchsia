// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/ref_counted_delayed_task.h"

#include <lib/async/cpp/task.h>
#include <zircon/errors.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

RefCountedDelayedTask::RefCountedDelayedTask(async_dispatcher_t* dispatcher,
                                             std::function<void()> task, zx::duration delay)
    : dispatcher_(dispatcher), task_(task), delay_(delay) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(task_);
}

void RefCountedDelayedTask::Acquire() {
  cancelable_task_.Cancel();
  ++ref_count_;
}

zx_status_t RefCountedDelayedTask::Release() {
  if (ref_count_ == 0) {
    FX_LOGS(ERROR) << "Unable to release, ref count is 0";
    return ZX_ERR_BAD_STATE;
  }

  --ref_count_;
  if (ref_count_ == 0) {
    cancelable_task_.Reset(task_);

    if (const auto status = async::PostDelayedTask(
            dispatcher_, [cb = cancelable_task_.callback()] { cb(); }, delay_);
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Error posting cancelable task to async loop";
      ref_count_ = 1;
      return status;
    }
  }

  return ZX_OK;
}

}  // namespace feedback
