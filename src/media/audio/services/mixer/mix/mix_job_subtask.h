// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIX_JOB_SUBTASK_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIX_JOB_SUBTASK_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>

#include <fbl/string_buffer.h>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

// Collects metrics for a during subtask of a mix job.
class MixJobSubtask {
 public:
  // Statistics about this task.
  struct Metrics {
    static constexpr size_t kMaxNameLength = 127;
    fbl::StringBuffer<kMaxNameLength> name;    // as a buffer to avoid heap allocations
    zx::duration wall_time;                    // total wall-clock time taken by this stage
    zx::duration cpu_time;                     // see zx_info_task_runtime.cpu_time
    zx::duration queue_time;                   // see zx_info_task_runtime.queue_time
    zx::duration page_fault_time;              // see zx_info_task_runtime.page_fault_time
    zx::duration kernel_lock_contention_time;  // see zx_info_task_runtime.lock_contention_time

    // Accumulate.
    Metrics& operator+=(const Metrics& rhs) {
      wall_time += rhs.wall_time;
      cpu_time += rhs.cpu_time;
      queue_time += rhs.queue_time;
      page_fault_time += rhs.page_fault_time;
      kernel_lock_contention_time += rhs.kernel_lock_contention_time;
      return *this;
    }
  };

  // Starts a new task.
  explicit MixJobSubtask(std::string_view name);

  // Signals the end of the task.
  void Done();

  // Report the current accumulated metrics.
  // Cannot be called before `Done()`.
  const Metrics& FinalMetrics() const {
    FX_CHECK(!running_);
    return metrics_;
  }

  MixJobSubtask(const MixJobSubtask&) = delete;
  MixJobSubtask& operator=(const MixJobSubtask&) = delete;
  MixJobSubtask(MixJobSubtask&&) = delete;
  MixJobSubtask& operator=(MixJobSubtask&&) = delete;

 private:
  struct StartInfo {
    zx_status_t status;
    zx_info_task_runtime_t info;
    zx::time time;
  };

  zx::unowned_thread thread_ = zx::thread::self();
  bool running_ = true;
  StartInfo start_;
  Metrics metrics_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIX_JOB_SUBTASK_H_
