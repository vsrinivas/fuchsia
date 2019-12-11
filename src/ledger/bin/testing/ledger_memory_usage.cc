// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/ledger_memory_usage.h"

#include <lib/zx/job.h>
#include <lib/zx/object.h>
#include <lib/zx/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <set>
#include <string>

#include <task-utils/walker.h>

#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

constexpr absl::string_view kLedgerBinaryName = "ledger.cmx";

// Retrieves the name of the task with the given handle. Returns true on
// success, or false otherwise.
bool GetTaskName(zx::unowned<zx::process>& task, std::string* name) {
  char task_name[ZX_MAX_NAME_LEN];
  zx_status_t status = task->get_property(ZX_PROP_NAME, task_name, ZX_MAX_NAME_LEN);
  if (status != ZX_OK) {
    // Failed to get the name of task.
    return false;
  }
  *name = convert::ToString(task_name);
  return true;
}

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

// |Walker| is a |TaskEnumerator| used to find the Ledger process and the
// corresponding handle.
//
// It assumes that the default job (as defined in zx::default_job()) has as
// parent a test environment, which contains the Ledger process as its
// descendent at depth 2. I.e. `ps` command would return:
//
//   j:...       <trace_environment_name>         # this has koid test_env_koid_
//     j:...                                      # this is the default job
//       p:...   <benchmark_name>.cmx
//     j:...
//       p:...   ledger.cmx
class Walker final : public TaskEnumerator {
 public:
  Walker() {
    zx_info_handle_basic_t info;
    LEDGER_CHECK(zx::job::default_job()->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                                                  nullptr, nullptr) == ZX_OK);
    test_env_koid_ = info.related_koid;
  }

  ~Walker() override = default;

  // TaskEnumerator:
  zx_status_t OnJob(int /*depth*/, zx_handle_t /*job*/, zx_koid_t koid,
                    zx_koid_t parent_koid) override {
    if (parent_koid == test_env_koid_) {
      test_env_children_.insert(koid);
    }
    return ZX_OK;
  }

  zx_status_t OnProcess(int /*depth*/, zx_handle_t task, zx_koid_t koid,
                        zx_koid_t parent_koid) override {
    zx::unowned<zx::process> unowned_task(task);
    std::string process_name;
    LEDGER_CHECK(GetTaskName(unowned_task, &process_name));
    // The parent of the Ledger process must be a child of |test_env_koid_|.
    if (process_name == kLedgerBinaryName &&
        test_env_children_.find(parent_koid) != test_env_children_.end()) {
      if (ledger_handle_.is_valid()) {
        // This is the second Ledger process we find: interrupt the iteration by
        // returning a status different than |ZK_OK|.
        return ZX_ERR_ALREADY_EXISTS;
      }
      // This process corresponds to the right instance of Ledger.
      LEDGER_CHECK(unowned_task->duplicate(ZX_RIGHT_SAME_RIGHTS, &ledger_handle_) == ZX_OK);
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
  bool has_on_job() const override { return true; }
  bool has_on_process() const override { return true; }

 private:
  // The koid of the parent of the default job. The Ledger process should also
  // have as grand-parent the process that corresponds to it.
  zx_koid_t test_env_koid_;
  // The set of the koids of jobs that are children of |test_env_koid_|.
  std::set<zx_koid_t> test_env_children_;

  zx::process ledger_handle_;
};

}  // namespace

LedgerMemoryEstimator::LedgerMemoryEstimator() = default;
LedgerMemoryEstimator::~LedgerMemoryEstimator() = default;

bool LedgerMemoryEstimator::Init() {
  LEDGER_DCHECK(!ledger_task_.is_valid()) << "Init should only be called once";
  Walker walker;
  zx_status_t status = walker.WalkRootJobTree();
  if (status == ZX_ERR_ALREADY_EXISTS) {
    LEDGER_LOG(ERROR) << "More than one Ledger processes are running in this test. Did you "
                         "set the environment name for this benchmark?";
    return false;
  }
  ledger_task_ = walker.TakeLedgerHandle();
  if (!ledger_task_.is_valid()) {
    LEDGER_LOG(ERROR) << "Failed to find a Ledger process.";
    return false;
  }
  return true;
}

bool LedgerMemoryEstimator::GetLedgerMemoryUsage(uint64_t* memory) {
  LEDGER_CHECK(ledger_task_);
  return GetMemoryUsageForTask(ledger_task_, memory);
}

bool GetCurrentProcessMemoryUsage(uint64_t* memory) {
  zx::unowned<zx::process> self = zx::process::self();
  return GetMemoryUsageForTask(*self, memory);
}

}  // namespace ledger
