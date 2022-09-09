// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/mix_job_subtask.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

MixJobSubtask::MixJobSubtask(std::string_view name) {
  metrics_.name.Append(name);

  // Start running the timer.
  start_.time = zx::clock::get_monotonic();
  start_.status =
      thread_->get_info(ZX_INFO_TASK_RUNTIME, &start_.info, sizeof(start_.info), nullptr, nullptr);

  // This should not happen.
  if (start_.status != ZX_OK) {
    FX_LOGS(WARNING) << "ZX_INFO_TASK_RUNTIME failed with status " << start_.status;
  }
}

void MixJobSubtask::Done() {
  FX_CHECK(running_);
  running_ = false;

  // Compute running times.
  metrics_.wall_time += zx::clock::get_monotonic() - start_.time;
  if (start_.status != ZX_OK) {
    return;
  }

  zx_info_task_runtime_t end_info;
  auto end_status =
      thread_->get_info(ZX_INFO_TASK_RUNTIME, &end_info, sizeof(end_info), nullptr, nullptr);
  if (end_status == ZX_OK) {
    metrics_.cpu_time += zx::nsec(end_info.cpu_time - start_.info.cpu_time);
    metrics_.queue_time += zx::nsec(end_info.queue_time - start_.info.queue_time);
    metrics_.page_fault_time += zx::nsec(end_info.page_fault_time - start_.info.page_fault_time);
    metrics_.kernel_lock_contention_time +=
        zx::nsec(end_info.lock_contention_time - start_.info.lock_contention_time);
  }
}

}  // namespace media_audio
