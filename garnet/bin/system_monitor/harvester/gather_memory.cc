// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_memory.h"

#include <zircon/status.h>

#include "harvester.h"
#include "src/lib/fxl/logging.h"

namespace harvester {

void GatherMemory::GatherDeviceProperties() {
  const std::string DEVICE_TOTAL = "memory:device_total_bytes";
  zx_info_kmem_stats_t stats;
  zx_status_t err = zx_object_get_info(RootResource(), ZX_INFO_KMEM_STATS,
                                       &stats, sizeof(stats),
                                       /*actual=*/nullptr, /*avail=*/nullptr);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << ZxErrorString("ZX_INFO_KMEM_STATS", err);
    return;
  }
  SampleList list;
  list.emplace_back(DEVICE_TOTAL, stats.total_bytes);
  DockyardProxyStatus status = Dockyard().SendSampleList(list);
  if (status != DockyardProxyStatus::OK) {
    FXL_LOG(ERROR) << "SendSampleList failed (" << status << ")";
  }
}

void GatherMemory::Gather() {
  zx_info_kmem_stats_t stats;
  zx_status_t err = zx_object_get_info(RootResource(), ZX_INFO_KMEM_STATS,
                                       &stats, sizeof(stats),
                                       /*actual=*/nullptr, /*avail=*/nullptr);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "ZX_INFO_KMEM_STATS error " << zx_status_get_string(err);
    return;
  }

  FXL_VLOG(2) << "free memory total " << stats.free_bytes << ", heap "
              << stats.free_heap_bytes << ", vmo " << stats.vmo_bytes
              << ", mmu " << stats.mmu_overhead_bytes << ", ipc "
              << stats.ipc_bytes;

  const std::string DEVICE_FREE = "memory:device_free_bytes";

  const std::string KERNEL_TOTAL = "memory:kernel_total_bytes";
  const std::string KERNEL_FREE = "memory:kernel_free_bytes";
  const std::string KERNEL_OTHER = "memory:kernel_other_bytes";

  const std::string VMO = "memory:vmo_bytes";
  const std::string MMU_OVERHEAD = "memory:mmu_overhead_bytes";
  const std::string IPC = "memory:ipc_bytes";
  const std::string OTHER = "memory:device_other_bytes";

  SampleList list;
  // Memory for the entire machine.
  // Note: stats.total_bytes is recorded by InitialData().
  list.push_back(std::make_pair(DEVICE_FREE, stats.free_bytes));
  // Memory in the kernel.
  list.push_back(std::make_pair(KERNEL_TOTAL, stats.total_heap_bytes));
  list.push_back(std::make_pair(KERNEL_FREE, stats.free_heap_bytes));
  list.push_back(std::make_pair(KERNEL_OTHER, stats.wired_bytes));
  // Categorized memory.
  list.push_back(std::make_pair(MMU_OVERHEAD, stats.mmu_overhead_bytes));
  list.push_back(std::make_pair(VMO, stats.vmo_bytes));
  list.push_back(std::make_pair(IPC, stats.ipc_bytes));
  list.push_back(std::make_pair(OTHER, stats.other_bytes));

  DockyardProxyStatus status = Dockyard().SendSampleList(list);
  if (status != DockyardProxyStatus::OK) {
    FXL_LOG(ERROR) << "SendSampleList failed (" << status << ")";
  }
}

}  // namespace harvester
