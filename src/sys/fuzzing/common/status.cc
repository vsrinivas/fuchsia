// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/status.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

Status CopyStatus(const Status& status) {
  Status copy;
  if (status.has_running()) {
    copy.set_running(status.running());
  }
  if (status.has_runs()) {
    copy.set_runs(status.runs());
  }
  if (status.has_elapsed()) {
    copy.set_elapsed(status.elapsed());
  }
  if (status.has_covered_pcs()) {
    copy.set_covered_pcs(status.covered_pcs());
  }
  if (status.has_covered_features()) {
    copy.set_covered_features(status.covered_features());
  }
  if (status.has_corpus_num_inputs()) {
    copy.set_corpus_num_inputs(status.corpus_num_inputs());
  }
  if (status.has_corpus_total_size()) {
    copy.set_corpus_total_size(status.corpus_total_size());
  }
  if (status.has_process_stats()) {
    auto* copied_stats = copy.mutable_process_stats();
    for (const auto& process_stats : status.process_stats()) {
      ProcessStats stats(process_stats);
      copied_stats->push_back(stats);
    }
  }
  return copy;
}

zx_status_t GetStatsForProcess(const zx::process& process, ProcessStats* out) {
  zx_info_handle_basic_t basic_info;
  auto status =
      process.get_info(ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info), nullptr, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to get basic handle info: " << zx_status_get_string(status);
    return status;
  }
  zx_info_task_stats_t task_stats;
  zx_info_task_runtime_t task_runtime;
  status = process.get_info(ZX_INFO_TASK_STATS, &task_stats, sizeof(task_stats), nullptr, nullptr);
  if (status == ZX_OK) {
    status = process.get_info(ZX_INFO_TASK_RUNTIME, &task_runtime, sizeof(task_runtime), nullptr,
                              nullptr);
  }
  if (status == ZX_ERR_BAD_HANDLE) {
    // Process terminated. This isn't unusual, and no warning is needed.
    return status;
  }
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to get task stats and/or runtime info: "
                     << zx_status_get_string(status);
    return status;
  }
  out->koid = basic_info.koid;
  out->mem_mapped_bytes = task_stats.mem_mapped_bytes;
  out->mem_private_bytes = task_stats.mem_private_bytes;
  out->mem_shared_bytes = task_stats.mem_shared_bytes;
  out->mem_scaled_shared_bytes = task_stats.mem_scaled_shared_bytes;
  out->cpu_time = task_runtime.cpu_time;
  out->queue_time = task_runtime.queue_time;
  out->page_fault_time = task_runtime.page_fault_time;
  out->lock_contention_time = task_runtime.lock_contention_time;
  return ZX_OK;
}

}  // namespace fuzzing
