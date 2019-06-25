// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/summary.h"

namespace memory {

Summary::Summary(const Capture& capture) : time_(capture.time()), kstats_(capture.kmem()) {
  std::unordered_map<zx_koid_t, std::unordered_set<zx_koid_t>> vmo_to_processes;
  auto const& koid_to_vmo = capture.koid_to_vmo();

  ProcessSummary kernel_summary(kstats_, koid_to_vmo);
  process_summaries_.push_back(kernel_summary);

  for (auto const& pair : capture.koid_to_process()) {
    auto process_koid = pair.first;
    auto& process = pair.second;
    ProcessSummary s(process_koid, process.name);
    for (auto vmo_koid : process.vmos) {
      do {
        vmo_to_processes[vmo_koid].insert(process_koid);
        s.vmos_.insert(vmo_koid);
        auto const& vmo = capture.vmo_for_koid(vmo_koid);
        // The parent koid could be missing.
        if (koid_to_vmo.find(vmo.parent_koid) == koid_to_vmo.end()) {
          break;
        }
        vmo_koid = vmo.parent_koid;
      } while (vmo_koid);
    }
    process_summaries_.push_back(s);
  }

  for (auto& s : process_summaries_) {
    for (auto const& v : s.vmos_) {
      auto const& vmo = capture.vmo_for_koid(v);
      auto share_count = vmo_to_processes[v].size();
      auto& name_sizes = s.name_to_sizes_[vmo.name];
      name_sizes.total_bytes += vmo.committed_bytes;
      s.sizes_.total_bytes += vmo.committed_bytes;
      if (share_count == 1) {
        name_sizes.private_bytes += vmo.committed_bytes;
        s.sizes_.private_bytes += vmo.committed_bytes;
        name_sizes.scaled_bytes += vmo.committed_bytes;
        s.sizes_.scaled_bytes += vmo.committed_bytes;
      } else {
        auto scaled_bytes = vmo.committed_bytes / share_count;
        name_sizes.scaled_bytes += scaled_bytes;
        s.sizes_.scaled_bytes += scaled_bytes;
      }
    }
  }
}

const zx_koid_t ProcessSummary::kKernelKoid = 1;

ProcessSummary::ProcessSummary(
    const zx_info_kmem_stats_t& kmem,
    const std::unordered_map<zx_koid_t, const zx_info_vmo_t>& koid_to_vmo)
    : koid_(kKernelKoid), name_("kernel") {
  uint64_t vmo_bytes = 0;
  for (const auto& pair : koid_to_vmo) {
    vmo_bytes += pair.second.committed_bytes;
  }
  auto kmem_vmo_bytes = kmem.vmo_bytes < vmo_bytes ? 0 : kmem.vmo_bytes - vmo_bytes;
  name_to_sizes_.emplace("heap", kmem.total_heap_bytes);
  name_to_sizes_.emplace("wired", kmem.wired_bytes);
  name_to_sizes_.emplace("mmu", kmem.mmu_overhead_bytes);
  name_to_sizes_.emplace("ipc", kmem.ipc_bytes);
  name_to_sizes_.emplace("other", kmem.other_bytes);
  name_to_sizes_.emplace("vmo", kmem_vmo_bytes);

  sizes_.private_bytes = sizes_.scaled_bytes = sizes_.total_bytes =
      kmem.wired_bytes + kmem.total_heap_bytes + kmem.mmu_overhead_bytes + kmem.ipc_bytes +
      kmem.other_bytes + kmem_vmo_bytes;
}

const Sizes& ProcessSummary::GetSizes(std::string name) const { return name_to_sizes_.at(name); }

}  // namespace memory
