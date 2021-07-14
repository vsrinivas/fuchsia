// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system_diagnostics_directory.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

#include "debug_info_retriever.h"
#include "lib/fpromise/promise.h"
#include "lib/inspect/cpp/inspector.h"
#include "lib/inspect/cpp/value_list.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"

using fxl::StringPrintf;

namespace {
struct ThreadInfo {
  zx_koid_t koid;
  fbl::String name;
  zx::thread thread;
  int64_t runtime;
};

static constexpr size_t kMaxThreads = 2048;

const char* obj_type_get_name(zx_obj_type_t type) {
  switch (type) {
    case ZX_OBJ_TYPE_NONE:
      return "none";
    case ZX_OBJ_TYPE_PROCESS:
      return "process";
    case ZX_OBJ_TYPE_THREAD:
      return "thread";
    case ZX_OBJ_TYPE_VMO:
      return "vmo";
    case ZX_OBJ_TYPE_CHANNEL:
      return "channel";
    case ZX_OBJ_TYPE_EVENT:
      return "event";
    case ZX_OBJ_TYPE_PORT:
      return "port";
    case ZX_OBJ_TYPE_INTERRUPT:
      return "interrupt";
    case ZX_OBJ_TYPE_PCI_DEVICE:
      return "pci_device";
    case ZX_OBJ_TYPE_LOG:
      return "log";
    case ZX_OBJ_TYPE_SOCKET:
      return "socket";
    case ZX_OBJ_TYPE_RESOURCE:
      return "resource";
    case ZX_OBJ_TYPE_EVENTPAIR:
      return "eventpair";
    case ZX_OBJ_TYPE_JOB:
      return "job";
    case ZX_OBJ_TYPE_VMAR:
      return "vmar";
    case ZX_OBJ_TYPE_FIFO:
      return "fifo";
    case ZX_OBJ_TYPE_GUEST:
      return "guest";
    case ZX_OBJ_TYPE_VCPU:
      return "vcpu";
    case ZX_OBJ_TYPE_TIMER:
      return "timer";
    case ZX_OBJ_TYPE_IOMMU:
      return "iommu";
    case ZX_OBJ_TYPE_BTI:
      return "bti";
    case ZX_OBJ_TYPE_PROFILE:
      return "profile";
    default:
      return "unknown";
  }
}

zx_status_t GetProcessHandleStats(const zx::process* process,
                                  zx_info_process_handle_stats_t* process_handle_stats) {
  zx_status_t status = process->get_info(ZX_INFO_PROCESS_HANDLE_STATS, process_handle_stats,
                                         sizeof(zx_info_process_handle_stats), nullptr, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "zx_object_get_info failed, status: " << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t GetTaskStats(const zx::process* process, zx_info_task_stats_t* task_stats) {
  zx_status_t status = process->get_info(ZX_INFO_TASK_STATS, task_stats,
                                         sizeof(zx_info_task_stats_t), nullptr, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "zx_object_get_info failed, status: " << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t GetThreadStats(zx_handle_t thread, zx_info_thread_stats_t* thread_stats) {
  zx_status_t status = zx_object_get_info(thread, ZX_INFO_THREAD_STATS, thread_stats,
                                          sizeof(zx_info_thread_stats_t), nullptr, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "zx_object_get_info failed, status: " << status << " thread: " << thread;
    return status;
  }

  return ZX_OK;
}

void GetThreads(const zx::process* process, std::vector<ThreadInfo>* out) {
  zx_koid_t thread_ids[kMaxThreads];
  size_t num_ids;
  if (process->get_info(ZX_INFO_PROCESS_THREADS, thread_ids, sizeof(zx_koid_t) * kMaxThreads,
                        &num_ids, nullptr) != ZX_OK) {
    return;
  }

  for (size_t i = 0; i < num_ids; i++) {
    zx::thread t;
    char name[ZX_MAX_NAME_LEN];
    zx_info_thread_stats_t stats = {};
    if (process->get_child(thread_ids[i], ZX_RIGHT_SAME_RIGHTS, &t) != ZX_OK) {
      return;
    }
    if (t.get_property(ZX_PROP_NAME, &name, ZX_MAX_NAME_LEN) != ZX_OK) {
      return;
    }
    if (GetThreadStats(t.get(), &stats) != ZX_OK) {
      return;
    }
    out->push_back(ThreadInfo{thread_ids[i], name, std::move(t), stats.total_runtime});
  }
}

fpromise::promise<inspect::Inspector> PopulateThreadInspect(zx::process* process) {
  // Make a 1mb buffer.
  inspect::Inspector inspector(inspect::InspectSettings{.maximum_size = 1024 * 1024});

  std::vector<ThreadInfo> threads;
  threads.reserve(kMaxThreads);
  GetThreads(process, &threads);

  for (const auto& thread : threads) {
    inspect::ValueList values;
    auto koid_string = StringPrintf("%lu", thread.koid);
    auto thread_obj = inspector.GetRoot().CreateChild(koid_string);
    thread_obj.CreateString("name", thread.name.data(), &values);
    thread_obj.CreateInt("total_runtime", thread.runtime, &values);
    auto stack_obj = thread_obj.CreateChild("stack");
    zx_koid_t koids[] = {thread.koid};
    stack_obj.CreateString(
        "dump",
        StringPrintf("\n%s", component::DebugInfoRetriever::GetInfo(process, koids, 1).data()),
        &values);

    values.emplace(std::move(thread_obj));
    values.emplace(std::move(stack_obj));
    inspector.emplace(std::move(values));
  }

  return fpromise::make_ok_promise(std::move(inspector));
}

fpromise::promise<inspect::Inspector> PopulateMemoryInspect(zx::process* process) {
  inspect::Inspector inspector;

  zx_info_task_stats_t task_stats = {};
  GetTaskStats(process, &task_stats);
  inspector.GetRoot().CreateUint("mapped_bytes", task_stats.mem_mapped_bytes, &inspector);
  inspector.GetRoot().CreateUint("private_bytes", task_stats.mem_private_bytes, &inspector);
  inspector.GetRoot().CreateUint("shared_bytes", task_stats.mem_shared_bytes, &inspector);
  inspector.GetRoot().CreateUint("scaled_shared_bytes", task_stats.mem_scaled_shared_bytes,
                                 &inspector);

  return fpromise::make_ok_promise(std::move(inspector));
}
}  // namespace

namespace component {

SystemDiagnosticsDirectory::SystemDiagnosticsDirectory(zx::process process)
    : process_(std::move(process)), inspector_() {
  inspector_.GetRoot().CreateLazyNode(
      "handle_count",
      [this]() -> fpromise::promise<inspect::Inspector> {
        inspect::Inspector inspector;
        zx_info_process_handle_stats_t process_handle_stats;
        if (GetProcessHandleStats(&process_, &process_handle_stats) != ZX_OK) {
          return fpromise::make_result_promise<inspect::Inspector>(fpromise::error());
        }

        for (zx_obj_type_t obj_type = ZX_OBJ_TYPE_NONE; obj_type < ZX_OBJ_TYPE_UPPER_BOUND;
             ++obj_type) {
          inspector.GetRoot().CreateUint(obj_type_get_name(obj_type),
                                         process_handle_stats.handle_count[obj_type], &inspector);
        }
        return fpromise::make_ok_promise(std::move(inspector));
      },
      &inspector_);
  inspector_.GetRoot().CreateLazyNode(
      "threads", [this] { return PopulateThreadInspect(&process_); }, &inspector_);
  inspector_.GetRoot().CreateLazyNode(
      "memory", [this] { return PopulateMemoryInspect(&process_); }, &inspector_);
}

}  // namespace component
