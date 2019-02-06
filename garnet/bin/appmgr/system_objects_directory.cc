// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system_objects_directory.h"

#include <fs/pseudo-file.h>
#include <lib/fxl/strings/string_printf.h>
#include <algorithm>
#include "debug_info_retriever.h"

using fxl::StringPrintf;

namespace component {

SystemObjectsDirectory::ThreadsDirectory::ThreadsDirectory(
    const zx::process* process)
    : ExposedObject("threads"), process_(process) {
  auto all_dir = component::ObjectDir::Make("all_thread_stacks");
  all_dir.set_prop("stacks", [this]() -> std::string {
    return StringPrintf("\n%s", DebugInfoRetriever::GetInfo(process_).data());
  });

  object_dir().set_child(all_dir.object());
  object_dir().set_children_callback(
      [this](component::Object::ObjectVector* out_children) {
        fbl::Vector<ThreadInfo> threads;
        threads.reserve(kMaxThreads);
        GetThreads(&threads);

        for (const auto& thread : threads) {
          auto koid_string = StringPrintf("%lu", thread.koid);
          auto thread_obj = component::ObjectDir::Make(koid_string);
          thread_obj.set_prop("koid", koid_string);
          thread_obj.set_prop("name", thread.name.data());
          zx_handle_t handle = thread.thread.get();
          zx_info_thread_stats_t thread_stats;
          thread_obj.set_metric(
              "total_runtime",
              UIntMetric(GetThreadStats(handle, &thread_stats) == ZX_OK
                             ? thread_stats.total_runtime
                             : 0u));
          out_children->push_back(thread_obj.object());

          auto koid = thread.koid;
          auto stack_obj = component::ObjectDir::Make("stack");
          stack_obj.set_prop("dump", [this, koid]() -> std::string {
            zx_koid_t koids[] = {koid};
            return StringPrintf(
                "\n%s", DebugInfoRetriever::GetInfo(process_, koids, 1).data());
          });
          thread_obj.set_child(stack_obj.object());
        }
      });
}

void SystemObjectsDirectory::ThreadsDirectory::GetThreads(
    fbl::Vector<ThreadInfo>* out) {
  zx_koid_t thread_ids[kMaxThreads];
  size_t num_ids;
  if (process_->get_info(ZX_INFO_PROCESS_THREADS, thread_ids,
                         sizeof(zx_koid_t) * kMaxThreads, &num_ids,
                         nullptr) != ZX_OK) {
    return;
  }

  for (size_t i = 0; i < num_ids; i++) {
    zx::thread t;
    char name[ZX_MAX_NAME_LEN];
    if (process_->get_child(thread_ids[i], ZX_RIGHT_SAME_RIGHTS, &t) != ZX_OK) {
      return;
    }
    if (t.get_property(ZX_PROP_NAME, &name, ZX_MAX_NAME_LEN) != ZX_OK) {
      return;
    }
    t.get_property(ZX_PROP_NAME, &name, ZX_MAX_NAME_LEN);
    out->push_back({thread_ids[i], name, std::move(t)});
  }
}

zx_status_t SystemObjectsDirectory::ThreadsDirectory::GetThreadStats(
    zx_handle_t thread, zx_info_thread_stats_t* thread_stats) {
  zx_status_t status =
      zx_object_get_info(thread, ZX_INFO_THREAD_STATS, thread_stats,
                         sizeof(zx_info_thread_stats_t), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_get_info failed, status: " << status
                   << " thread: " << thread;
    return status;
  }

  return ZX_OK;
}

SystemObjectsDirectory::MemoryDirectory::MemoryDirectory(
    const zx::process* process)
    : ExposedObject("memory"), process_(process) {
  zx_info_task_stats_t task_stats;
  if (process_->get_info(ZX_INFO_TASK_STATS, &task_stats,
                         sizeof(zx_info_task_stats_t), nullptr,
                         nullptr) != ZX_OK) {
    return;
  }

  object_dir().set_metric(
      "mapped_bytes", component::CallbackMetric([this](component::Metric* out) {
        zx_info_task_stats_t task_stats;
        if (GetTaskStats(&task_stats) != ZX_OK)
          return;
        out->SetUInt(task_stats.mem_mapped_bytes);
      }));

  object_dir().set_metric(
      "private_bytes",
      component::CallbackMetric([this](component::Metric* out) {
        zx_info_task_stats_t task_stats;
        if (GetTaskStats(&task_stats) != ZX_OK)
          return;
        out->SetUInt(task_stats.mem_private_bytes);
      }));

  object_dir().set_metric(
      "shared_bytes", component::CallbackMetric([this](component::Metric* out) {
        zx_info_task_stats_t task_stats;
        if (GetTaskStats(&task_stats) != ZX_OK)
          return;
        out->SetUInt(task_stats.mem_shared_bytes);
      }));

  object_dir().set_metric(
      "scaled_shared_bytes",
      component::CallbackMetric([this](component::Metric* out) {
        zx_info_task_stats_t task_stats;
        if (GetTaskStats(&task_stats) != ZX_OK)
          return;
        out->SetUInt(task_stats.mem_scaled_shared_bytes);
      }));
}

zx_status_t SystemObjectsDirectory::MemoryDirectory::GetTaskStats(
    zx_info_task_stats_t* task_stats) {
  zx_status_t status =
      process_->get_info(ZX_INFO_TASK_STATS, task_stats,
                         sizeof(zx_info_task_stats_t), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_get_info failed, status: " << status;
    return status;
  }

  return ZX_OK;
}

}  // namespace component
