// Copyright 2021 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/task_runtime_stats.h"

#include <lib/affine/ratio.h>
#include <platform.h>

void TaskRuntimeStats::AccumulateRuntimeTo(zx_info_task_runtime_t* info) const {
  info->cpu_time = zx_duration_add_duration(info->cpu_time, cpu_time);
  info->queue_time = zx_duration_add_duration(info->queue_time, queue_time);

  const affine::Ratio& ticks_to_time = platform_get_ticks_to_time_ratio();
  info->page_fault_time =
      zx_duration_add_duration(info->page_fault_time, ticks_to_time.Scale(page_fault_ticks));
  info->lock_contention_time = zx_duration_add_duration(info->lock_contention_time,
                                                        ticks_to_time.Scale(lock_contention_ticks));
}
