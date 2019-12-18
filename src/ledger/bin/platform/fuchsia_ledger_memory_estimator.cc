// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/platform/fuchsia_ledger_memory_estimator.h"

#include <lib/zx/job.h>
#include <lib/zx/object.h>
#include <lib/zx/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <set>
#include <string>

#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

// Retrieves the private bytes used by the given task. Returns true on success,
// or false otherwise.
bool GetMemoryUsageForTask(const zx::process& task, uint64_t* memory) {
  zx_info_task_stats_t info;
  zx_status_t status = task.get_info(ZX_INFO_TASK_STATS, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return false;
  }
  *memory = info.mem_private_bytes;
  return true;
}

}  // namespace

bool FuchsiaLedgerMemoryEstimator::GetLedgerMemoryUsage(uint64_t* /*memory*/) {
  LEDGER_NOTIMPLEMENTED();
  return false;
}

bool FuchsiaLedgerMemoryEstimator::GetCurrentProcessMemoryUsage(uint64_t* memory) {
  zx::unowned<zx::process> self = zx::process::self();
  return GetMemoryUsageForTask(*self, memory);
}

}  // namespace ledger
