// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/capture.h"

#include <fuchsia/kernel/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <task-utils/walker.h>

namespace memory {

class OSImpl : public OS, public TaskEnumerator {
 private:
  zx_status_t GetKernelStats(
      std::unique_ptr<llcpp::fuchsia::kernel::Stats::SyncClient>* stats) override {
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return status;
    }
    const char* kernel_stats_svc = "/svc/fuchsia.kernel.Stats";
    status = fdio_service_connect(kernel_stats_svc, remote.release());
    if (status != ZX_OK) {
      return status;
    }

    *stats = std::make_unique<llcpp::fuchsia::kernel::Stats::SyncClient>(std::move(local));
    return ZX_OK;
  }

  zx_handle_t ProcessSelf() override { return zx_process_self(); }
  zx_time_t GetMonotonic() override { return zx_clock_get_monotonic(); }

  zx_status_t GetProcesses(
      fit::function<zx_status_t(int, zx_handle_t, zx_koid_t, zx_koid_t)> cb) override {
    TRACE_DURATION("memory_metrics", "Capture::GetProcesses");
    cb_ = std::move(cb);
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return status;
    }

    const char* root_job_svc = "/svc/fuchsia.kernel.RootJobForInspect";
    status = fdio_service_connect(root_job_svc, remote.release());
    if (status != ZX_OK) {
      return status;
    }

    zx::job root_job;
    status = fuchsia_kernel_RootJobForInspectGet(local.get(), root_job.reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }

    return WalkJobTree(root_job.get());
  }

  zx_status_t OnProcess(int depth, zx_handle_t handle, zx_koid_t koid,
                        zx_koid_t parent_koid) override {
    return cb_(depth, handle, koid, parent_koid);
  }

  zx_status_t GetProperty(zx_handle_t handle, uint32_t property, void* value,
                          size_t name_len) override {
    return zx_object_get_property(handle, property, value, name_len);
  }

  zx_status_t GetInfo(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                      size_t* actual, size_t* avail) override {
    TRACE_DURATION("memory_metrics", "OSImpl::GetInfo", "topic", topic, "buffer_size", buffer_size);
    return zx_object_get_info(handle, topic, buffer, buffer_size, actual, avail);
  }

  zx_status_t GetKernelMemoryStats(llcpp::fuchsia::kernel::Stats::SyncClient* stats_client,
                                   zx_info_kmem_stats_extended_t* kmem) override {
    TRACE_DURATION("memory_metrics", "Capture::GetKernelMemoryStats");
    if (stats_client == nullptr) {
      return ZX_ERR_BAD_STATE;
    }
    auto result = stats_client->GetMemoryStatsExtended();
    if (result.status() != ZX_OK) {
      return result.status();
    }
    const auto& stats = result.Unwrap()->stats;
    kmem->total_bytes = stats.total_bytes();
    kmem->free_bytes = stats.free_bytes();
    kmem->wired_bytes = stats.wired_bytes();
    kmem->total_heap_bytes = stats.total_heap_bytes();
    kmem->free_heap_bytes = stats.free_heap_bytes();
    kmem->vmo_bytes = stats.vmo_bytes();
    kmem->vmo_pager_total_bytes = stats.vmo_pager_total_bytes();
    kmem->vmo_pager_newest_bytes = stats.vmo_pager_newest_bytes();
    kmem->vmo_pager_oldest_bytes = stats.vmo_pager_oldest_bytes();
    kmem->vmo_discardable_locked_bytes = stats.vmo_discardable_locked_bytes();
    kmem->vmo_discardable_unlocked_bytes = stats.vmo_discardable_unlocked_bytes();
    kmem->mmu_overhead_bytes = stats.mmu_overhead_bytes();
    kmem->ipc_bytes = stats.ipc_bytes();
    kmem->other_bytes = stats.other_bytes();
    return ZX_OK;
  }

  bool has_on_process() const final { return true; }

  fit::function<zx_status_t(int /* depth */, zx_handle_t /* handle */, zx_koid_t /* koid */,
                            zx_koid_t /* parent_koid */)>
      cb_;
};

const std::vector<std::string> Capture::kDefaultRootedVmoNames = {
    "SysmemContiguousPool", "SysmemAmlogicProtectedPool", "Sysmem-core"};
// static.
zx_status_t Capture::GetCaptureState(CaptureState* state) {
  OSImpl osImpl;
  return GetCaptureState(state, &osImpl);
}

zx_status_t Capture::GetCaptureState(CaptureState* state, OS* os) {
  TRACE_DURATION("memory_metrics", "Capture::GetCaptureState");
  zx_status_t err = os->GetKernelStats(&state->stats_client);
  if (err != ZX_OK) {
    return err;
  }

  zx_info_handle_basic_t info;
  err = os->GetInfo(os->ProcessSelf(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (err != ZX_OK) {
    return err;
  }

  state->self_koid = info.koid;
  return ZX_OK;
}

// static.
zx_status_t Capture::GetCapture(Capture* capture, const CaptureState& state, CaptureLevel level,
                                const std::vector<std::string>& rooted_vmo_names) {
  OSImpl osImpl;
  return GetCapture(capture, state, level, &osImpl, rooted_vmo_names);
}

zx_status_t Capture::GetCapture(Capture* capture, const CaptureState& state, CaptureLevel level,
                                OS* os, const std::vector<std::string>& rooted_vmo_names) {
  TRACE_DURATION("memory_metrics", "Capture::GetCapture");
  capture->time_ = os->GetMonotonic();

  zx_status_t err = os->GetKernelMemoryStats(state.stats_client.get(), &capture->kmem_);
  if (err != ZX_OK) {
    return err;
  }

  if (level == KMEM) {
    return ZX_OK;
  }

  err = os->GetProcesses(
      [&state, capture, &os](int depth, zx_handle_t handle, zx_koid_t koid, zx_koid_t parent_koid) {
        if (koid == state.self_koid) {
          return ZX_OK;
        }
        char name[ZX_MAX_NAME_LEN];
        zx_status_t s = os->GetProperty(handle, ZX_PROP_NAME, name, ZX_MAX_NAME_LEN);
        if (s != ZX_OK) {
          return s == ZX_ERR_BAD_STATE ? ZX_OK : s;
        }

        TRACE_DURATION_BEGIN("memory_metrics", "Capture::GetProcesses::GetVMOCount");
        size_t num_vmos;
        s = os->GetInfo(handle, ZX_INFO_PROCESS_VMOS, nullptr, 0, nullptr, &num_vmos);
        if (s != ZX_OK) {
          return s == ZX_ERR_BAD_STATE ? ZX_OK : s;
        }
        TRACE_DURATION_END("memory_metrics", "Capture::GetProcesses::GetVMOCount");

        TRACE_DURATION_BEGIN("memory_metrics", "Capture::GetProcesses::GetVMOs");
        auto vmos = std::make_unique<zx_info_vmo_t[]>(num_vmos);
        s = os->GetInfo(handle, ZX_INFO_PROCESS_VMOS, vmos.get(), num_vmos * sizeof(zx_info_vmo_t),
                        &num_vmos, nullptr);
        if (s != ZX_OK) {
          return s == ZX_ERR_BAD_STATE ? ZX_OK : s;
        }
        TRACE_DURATION_END("memory_metrics", "Capture::GetProcesses::GetVMOs");

        TRACE_DURATION_BEGIN("memory_metrics", "Capture::GetProcesses::UniqueProcessVMOs");
        std::unordered_map<zx_koid_t, const zx_info_vmo_t*> unique_vmos;
        unique_vmos.reserve(num_vmos);
        for (size_t i = 0; i < num_vmos; i++) {
          const auto* vmo_info = vmos.get() + i;
          unique_vmos.try_emplace(vmo_info->koid, vmo_info);
        }
        TRACE_DURATION_END("memory_metrics", "Capture::GetProcesses::UniqueProcessVMOs");

        TRACE_DURATION_BEGIN("memory_metrics", "Capture::GetProcesses::InsertProcess");
        auto [it, _] = capture->koid_to_process_.insert({koid, {}});
        auto& process = it->second;
        process.koid = koid;
        strncpy(process.name, name, ZX_MAX_NAME_LEN);
        TRACE_DURATION_END("memory_metrics", "Capture::GetProcesses::InsertProcess");

        TRACE_DURATION_BEGIN("memory_metrics", "Capture::GetProcesses::UniqueVMOs");
        process.vmos.reserve(unique_vmos.size());
        for (const auto& [koid, vmo] : unique_vmos) {
          capture->koid_to_vmo_.try_emplace(koid, *vmo);
          process.vmos.push_back(koid);
        }
        TRACE_DURATION_END("memory_metrics", "Capture::GetProcesses::UniqueVMOs");

        return ZX_OK;
      });
  capture->ReallocateDescendents(rooted_vmo_names);
  return err;
}

// Descendents of this vmo will have their allocated_bytes treated as an allocation of their
// immediate parent. This supports a usage pattern where a potentially large allocation is done
// and then slices are given to read / write children. In this case the children have no
// committed_bytes of their own. For accounting purposes it gives more clarity to push the
// committed bytes to the lowest points in the tree, where the vmo names give more specific
// meanings.
void Capture::ReallocateDescendents(zx_koid_t parent_koid) {
  Vmo& parent = koid_to_vmo_.at(parent_koid);
  for (auto& [_, child] : koid_to_vmo_) {
    if (child.parent_koid == parent_koid) {
      uint64_t reallocated_bytes = std::min(parent.committed_bytes, child.allocated_bytes);
      parent.committed_bytes -= reallocated_bytes;
      child.committed_bytes = reallocated_bytes;
      ReallocateDescendents(child.koid);
    }
  }
}

// See the above description of ReallocateDescendents(zx_koid_t) for the specific behavior for each
// vmo that has a name listed in rooted_vmo_names.
void Capture::ReallocateDescendents(const std::vector<std::string>& rooted_vmo_names) {
  TRACE_DURATION("memory_metrics", "Capture::ReallocateDescendents");
  for (const auto& vmo_name : rooted_vmo_names) {
    for (const auto& [koid, vmo] : koid_to_vmo_) {
      if (vmo.name == vmo_name) {
        ReallocateDescendents(koid);
      }
    }
  }
}
}  // namespace memory
