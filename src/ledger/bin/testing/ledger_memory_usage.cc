// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/ledger_memory_usage.h"

#include <lib/zx/object.h>
#include <lib/zx/process.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_view.h>
#include <task-utils/walker.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <string>

namespace ledger {
namespace {

constexpr fxl::StringView kLedgerBinaryName = "ledger.cmx";

// Retrieves the name of the task with the given handle. Returns true on
// success, or false otherwise.
bool GetTaskName(zx::unowned<zx::process>& task, std::string* name) {
  char task_name[ZX_MAX_NAME_LEN];
  zx_status_t status =
      task->get_property(ZX_PROP_NAME, task_name, ZX_MAX_NAME_LEN);
  if (status != ZX_OK) {
    // Failed to get the name of task.
    return false;
  }
  *name = std::string(task_name);
  return true;
}

// Retrieves the private bytes used by the given task. Returns true on success,
// or false otherwise.
bool GetMemoryUsageForTask(zx::process& task, uint64_t* memory) {
  zx_info_task_stats_t info;
  zx_status_t status =
      task.get_info(ZX_INFO_TASK_STATS, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return false;
  }
  *memory = info.mem_private_bytes;
  return true;
}

// |Walker| is a |TaskEnumerator| used to find the Ledger process and the
// corresponding handle.
class Walker final : public TaskEnumerator {
 public:
  Walker() = default;
  ~Walker() = default;

  // TaskEnumerator:
  zx_status_t OnProcess(int depth, zx_handle_t task, zx_koid_t koid,
                        zx_koid_t /*pkoid*/) override {
    zx::unowned<zx::process> unowned_task(task);
    std::string process_name;
    FXL_CHECK(GetTaskName(unowned_task, &process_name));
    if (process_name == kLedgerBinaryName) {
      if (ledger_handle_.is_valid()) {
        // This is the second Ledger process we find: interrupt the iteration by
        // returning a status different than |ZK_OK|.
        return ZX_ERR_ALREADY_EXISTS;
      }
      // This process corresponds to Ledger.
      FXL_CHECK(unowned_task->duplicate(ZX_RIGHT_SAME_RIGHTS,
                                        &ledger_handle_) == ZX_OK);
    }
    return ZX_OK;
  }

  // Returns the handle of the Ledger process, or an invalid handle if it was
  // not found. This method should be called only after a successful termination
  // of |Walker::WalkRootJobTree()|. The caller takes ownership of the returned
  // handle, meaning that this method can only be called once.
  zx::process TakeLedgerHandle() { return std::move(ledger_handle_); }

 protected:
  // TaskEnumerator:
  bool has_on_process() const override { return true; }

 private:
  zx::process ledger_handle_;
};

}  // namespace

LedgerMemoryEstimator::LedgerMemoryEstimator() = default;
LedgerMemoryEstimator::~LedgerMemoryEstimator() = default;

bool LedgerMemoryEstimator::Init() {
  FXL_DCHECK(!ledger_task_.is_valid()) << "Init should only be called once";
  Walker walker;
  zx_status_t status = walker.WalkRootJobTree();
  if (status == ZX_ERR_ALREADY_EXISTS) {
    // TODO(nellyv): Update so that we know how which Ledger corresponds to the
    // test being executed.
    FXL_LOG(ERROR) << "More than one Ledger processes are running.";
    return false;
  }
  ledger_task_ = walker.TakeLedgerHandle();
  if (!ledger_task_.is_valid()) {
    FXL_LOG(ERROR) << "Failed to find a Ledger process.";
    return false;
  }
  return true;
}

bool LedgerMemoryEstimator::GetLedgerMemoryUsage(uint64_t* memory) {
  FXL_CHECK(ledger_task_);
  return GetMemoryUsageForTask(ledger_task_, memory);
}

}  // namespace ledger
