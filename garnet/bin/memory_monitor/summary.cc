// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/memory_monitor/summary.h"

namespace memory {

Summary::Summary(const Capture& capture)
    : time_(capture.time()), kstats_(capture.kmem()) {
  std::unordered_map<zx_koid_t, std::unordered_set<zx_koid_t>>
      vmo_to_processes;
  auto const& koid_to_vmo = capture.koid_to_vmo();
  for (auto const& pair : capture.koid_to_process()) {
    auto process_koid = pair.first;
    auto& process = pair.second;
    ProcessSummary s(process_koid, process.name);
    for (auto vmo_koid: process.vmos) {
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

  for (auto& s: process_summaries_) {
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

const Sizes& ProcessSummary::GetSizes(std::string name) const {
  return name_to_sizes_.at(name);
}

}  // namespace memory
