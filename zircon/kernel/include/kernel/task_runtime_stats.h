// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_TASK_RUNTIME_STATS_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_TASK_RUNTIME_STATS_H_

#include <zircon/syscalls/object.h>
#include <zircon/time.h>

// Holds information about the runtime of a task.
struct TaskRuntimeStats {
  // The total amount of time spent running on a CPU.
  zx_duration_t cpu_time = 0;

  // The total amount of time spent ready to start running.
  zx_duration_t queue_time = 0;

  // Add another TaskRuntimeStats into this one.
  void Add(const TaskRuntimeStats& other) {
    cpu_time = zx_duration_add_duration(cpu_time, other.cpu_time);
    queue_time = zx_duration_add_duration(queue_time, other.queue_time);
  }

  void AccumulateRuntimeTo(zx_info_task_runtime_t* info) const {
    info->cpu_time = zx_duration_add_duration(info->cpu_time, cpu_time);
    info->queue_time = zx_duration_add_duration(info->queue_time, queue_time);
  }
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_TASK_RUNTIME_STATS_H_
