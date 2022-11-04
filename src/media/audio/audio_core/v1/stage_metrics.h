// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_STAGE_METRICS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_STAGE_METRICS_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <stdint.h>

#include <optional>
#include <string>

#include <fbl/string_buffer.h>

namespace media::audio {

// Statistics about a pipeline stage.
struct StageMetrics {
  static constexpr size_t kMaxNameLength = 127;
  fbl::StringBuffer<kMaxNameLength> name;    // as a buffer to avoid heap allocations
  zx::duration wall_time;                    // total wall-clock time taken by this stage
  zx::duration cpu_time;                     // see zx_info_task_runtime.cpu_time
  zx::duration queue_time;                   // see zx_info_task_runtime.queue_time
  zx::duration page_fault_time;              // see zx_info_task_runtime.page_fault_time
  zx::duration kernel_lock_contention_time;  // see zx_info_task_runtime.lock_contention_time

  // Accumulate.
  StageMetrics& operator+=(const StageMetrics& rhs) {
    wall_time += rhs.wall_time;
    cpu_time += rhs.cpu_time;
    queue_time += rhs.queue_time;
    page_fault_time += rhs.page_fault_time;
    kernel_lock_contention_time += rhs.kernel_lock_contention_time;
    return *this;
  }
};

// A timer which accumulates a StageMetrics object to represent the total time spent between
// each pair of (Start, Stop) calls. Not thread safe.
class StageMetricsTimer {
 public:
  explicit StageMetricsTimer(std::string_view name) { metrics_.name.Append(name); }

  // Start running the timer.
  void Start() {
    thread_ = zx::thread::self();
    running_ = true;
    start_.time = zx::clock::get_monotonic();
    start_.status = thread_->get_info(ZX_INFO_TASK_RUNTIME, &start_.info, sizeof(start_.info),
                                      nullptr, nullptr);
  }

  // Stop running the timer.
  void Stop() {
    running_ = false;
    metrics_.wall_time += zx::clock::get_monotonic() - start_.time;

    if (start_.status == ZX_OK) {
      zx_info_task_runtime_t end_info;
      auto end_status =
          thread_->get_info(ZX_INFO_TASK_RUNTIME, &end_info, sizeof(end_info), nullptr, nullptr);
      if (end_status == ZX_OK) {
        metrics_.cpu_time += zx::nsec(end_info.cpu_time - start_.info.cpu_time);
        metrics_.queue_time += zx::nsec(end_info.queue_time - start_.info.queue_time);
        metrics_.page_fault_time +=
            zx::nsec(end_info.page_fault_time - start_.info.page_fault_time);
        metrics_.kernel_lock_contention_time +=
            zx::nsec(end_info.lock_contention_time - start_.info.lock_contention_time);
      }
    }
  }

  // Report the current accumulated metrics.
  // Cannot be called while the timer is running; the timer must be stopped.
  const StageMetrics& Metrics() const {
    FX_CHECK(!running_);
    return metrics_;
  }

 private:
  struct StartInfo {
    zx_status_t status;
    zx_info_task_runtime_t info;
    zx::time time;
  };

  zx::unowned_thread thread_;
  StageMetrics metrics_;
  StartInfo start_;
  bool running_ = false;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_STAGE_METRICS_H_
