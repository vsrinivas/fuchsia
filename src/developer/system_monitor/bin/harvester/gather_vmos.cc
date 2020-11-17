// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_vmos.h"

#include <vector>

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/status.h>

#include "harvester.h"
#include "sample_bundle.h"
#include "task_tree.h"

namespace harvester {

namespace {

const size_t kNumInitialVmos = 128;

// When VMO collection is enabled, |kNumExtraVmoScans| processes known not to
// have "rooted" VMOs will still be scanned (just in case they acquire access in
// the meantime).
const size_t kNumExtraVmoScans = 3;

// Sysmem VMO names, as found in src/devices/sysmem/drivers/sysmem/device.cc.
const VmoMap<std::string> kRootedVmoNames = {
  "Sysmem-core",
  "Sysmem-contig-core",
  // TODO(fxbug.dev/64288): Determine if "Sysmem-external-heap" belongs here.
  "SysmemContiguousPool",
  "SysmemAmlogicProtectedPool",
};

VmoMap<std::string> DockyardPathsForNames(const VmoMap<std::string> names) {
  VmoMap<std::string> paths;
  for (size_t i = 0; i < kNumRootedVmos; ++i) {
    paths[i] = std::string("vmo_") + names[i];
  }

  return paths;
}

// Sysmem VMO paths for dockyard upload.
const VmoMap<std::string> kRootedMemoryPaths =
    DockyardPathsForNames(kRootedVmoNames);

}  // namespace

void GatherVmos::GatherVmosForProcess(zx_handle_t process,
                                      zx_koid_t process_koid,
                                      std::vector<zx_koid_t>& new_vmos) {
  TRACE_DURATION("harvester", "GatherVmos::GatherVmosForProcess");
  zx_status_t status;

  // Get the koids for the VMOs belonging to this process.
  std::vector<zx_info_vmo_t> vmos(kNumInitialVmos);

  TRACE_DURATION_BEGIN("harvester", "GetChildren<VMO>", "koid",
                       TA_KOID(process_koid));
  status = os_->GetChildren<zx_info_vmo_t>(process, process_koid,
                                           ZX_INFO_PROCESS_VMOS,
                                           "ZX_INFO_PROCESS_VMOS", vmos);
  TRACE_DURATION_END("harvester", "GetChildren<VMO>");
  if (status != ZX_OK) {
    return;
  }

  std::unordered_set<zx_koid_t> vmo_koids;
  vmo_koids.reserve(vmos.size());

  TRACE_DURATION_BEGIN("harvester", "Iterate VMOs", "num_vmos", vmos.size());
  for (const zx_info_vmo_t& vmo : vmos) {
    // Insert returns a pair<iterator, bool>, where bool is true if this is a
    // new element.
    auto pair = vmo_koids.insert(vmo.koid);
    if (pair.second) {
      new_vmos.push_back(vmo.koid);
    }
    koids_to_vmos_.insert({vmo.koid, vmo});

    if (vmo.parent_koid != 0) {
      // If this VMO has a parent, link the two.
      vmo_forest_.Union(vmo.koid, vmo.parent_koid);
    } else {
      // If this VMO has no parent, it _may be_ a rooted VMO.
      auto name_it = std::find(kRootedVmoNames.begin(), kRootedVmoNames.end(),
                               vmo.name);
      if (name_it != kRootedVmoNames.end()) {
        rooted_vmos_.insert(vmo.koid);
      }
    }
  }
  TRACE_DURATION_END("harvester", "Iterate VMOs");

  process_to_vmos_.insert_or_assign(process_koid, std::move(vmo_koids));
}

void GatherVmos::DoSparseVmoUpdate(
    const TaskTree& task_tree,
    std::vector<zx_koid_t>& new_vmos,
    std::unordered_set<zx_koid_t>& live_process_koids) {
  TRACE_DURATION_BEGIN("harvester", "GatherVmos::DoSparseVmoUpdate");
  size_t trace_num_expected_scans = 0;
  size_t trace_num_extra_scans = 0;

  live_process_koids.reserve(task_tree.Processes().size());

  // Prefill process_scan_queue_ if it's empty. This should only happen once,
  // but can't be done inline with the loop below.
  if (process_scan_queue_.empty()) {
    for (const TaskTree::Task& process : task_tree.Processes()) {
      process_scan_queue_.emplace_back(process);
    }
  }

  // NOTE: Does sparse VMO scans over the process universe for efficiency.
  // 1. Always scan the first time a process is seen.
  // 2. Always rescan processes known to have rooted VMOs.
  // 3. Do sparse rescans over the rest of the process universe.

  // Parts 1,2 of sparse scans (above).
  for (const TaskTree::Task& process : task_tree.Processes()) {
    zx_handle_t handle = process.handle;
    zx_koid_t koid = process.koid;
    live_process_koids.insert(koid);

    bool is_new_process = last_seen_processes_.count(koid) == 0;
    bool is_rooted_process = processes_with_rooted_vmos_.count(koid) != 0;
    if (is_new_process || is_rooted_process) {
      GatherVmosForProcess(handle, koid, new_vmos);
      ++trace_num_expected_scans;
    }

    // If this is a new process, add to the back of |process_scan_queue_|.
    if (is_new_process) {
      process_scan_queue_.emplace_back(process);
    }
  }

  // Part 3 of sparse scans (above).
  for (size_t i = 0; i < kNumExtraVmoScans && !process_scan_queue_.empty(); ) {
    const TaskTree::Task& process = process_scan_queue_.front();
    zx_handle_t handle = process.handle;
    zx_koid_t koid = process.koid;
    process_scan_queue_.pop_front();

    bool is_new_process = last_seen_processes_.count(koid) == 0;
    bool is_dead_process = live_process_koids.count(koid) == 0;
    bool is_rooted_process = processes_with_rooted_vmos_.count(koid) != 0;

    // Do nothing if this process is no longer live.
    if (is_dead_process) {
      continue;
    }

    // Scan processes not already scanned above.
    if (!is_new_process && !is_rooted_process) {
      GatherVmosForProcess(handle, koid, new_vmos);
      ++trace_num_extra_scans;
    }

    process_scan_queue_.emplace_back(process);
    ++i;
  }
  TRACE_DURATION_END("harvester", "GatherVmos::DoSparseVmoUpdate",
                     "processes scanned",
                     trace_num_expected_scans + trace_num_extra_scans,
                     "extra scans",
                     trace_num_extra_scans);
}

bool GatherVmos::VmoHasRootedAncestor(zx_koid_t vmo_koid) {
  return rooted_vmo_descendants_.count(vmo_koid) != 0;
}

void GatherVmos::CleanProcessToVmos(
    const std::unordered_set<zx_koid_t>& live_process_koids) {
  TRACE_DURATION("harvester", "GatherVmos::CleanProcessToVmos");

  // Prune dead processes from process_to_vmos_. Rough equivalent of C++20's
  // erase_if().
  for (auto it = process_to_vmos_.begin(); it != process_to_vmos_.end(); ) {
    zx_koid_t koid = it->first;
    if (live_process_koids.count(koid) == 0) {
      it = process_to_vmos_.erase(it);
    } else {
      ++it;
    }
  }
}

void GatherVmos::BuildRootedVmoDescendants(
    const std::vector<zx_koid_t>& new_vmos) {
  TRACE_DURATION("harvester", "GatherVmos::BuildRootedVmoDescendants");

  // Optimization: Given N VMOs and K rooted VMOs, using vmo_forest_.InSameSet()
  // here would result in O(K * N) Find() calls. Instead, here we're caching
  // the Find() calls for the K rooted VMOs. This gives a much better time of
  // O(K + N) Find() calls.

  // Part 1: cache the K Find() calls.
  std::unordered_set<zx_koid_t> representatives_of_rooted_vmos;
  for (zx_koid_t koid : rooted_vmos_) {
    if (koid != 0) {
      representatives_of_rooted_vmos.insert(vmo_forest_.Find(koid));
    }
  }

  // Part 2: run N Find() calls over the VMO universe.
  for (zx_koid_t koid : new_vmos) {
    zx_koid_t vmo_representative = vmo_forest_.Find(koid);

    // Check if this VMO koid is in the same set as one of the rooted VMOs.
    if (representatives_of_rooted_vmos.count(vmo_representative) != 0) {
      rooted_vmo_descendants_.insert(koid);
    }
  }
}

void GatherVmos::BuildProcessesWithRootedVmos() {
  TRACE_DURATION("harvester", "GatherVmos::BuildProcessesWithRootedVmos");

  processes_with_rooted_vmos_.clear();
  for (const auto& [process_koid, vmo_koids] : process_to_vmos_) {
    for (zx_koid_t vmo_koid : vmo_koids) {
      if (VmoHasRootedAncestor(vmo_koid)) {
        processes_with_rooted_vmos_.insert(process_koid);
        break;
      }
    }
  }
}

std::map<zx_koid_t, GatherVmos::Vmo> GatherVmos::BuildVmoData() {
  TRACE_DURATION("harvester", "GatherVmos::BuildVmoData");

  std::map<zx_koid_t, Vmo> vmo_data;

  // Pass 1: Copy over each VMO.
  // We only care about rooted VMOs and their descendants. Don't bother copying
  // anything else.
  for (zx_koid_t vmo_koid : rooted_vmo_descendants_) {
    vmo_data.emplace(vmo_koid, Vmo(koids_to_vmos_[vmo_koid]));
  }

  // Pass 2: Ignore bytes in a parent VMO that are accounted to a child VMO.
  std::unordered_set<zx_koid_t> vmos_with_children;
  for (const auto& [vmo_koid, vmo] : vmo_data) {
    if (vmo.parent_koid == 0) {
      continue;
    }

    if (vmo_data.count(vmo.parent_koid) == 0) {
      FX_LOGS(ERROR) << "vmo's (" << vmo_koid << ") parent koid ("
                     << vmo.parent_koid << ") is not in vmo_data map.";
    }

    Vmo& parent_vmo = vmo_data[vmo.parent_koid];
    parent_vmo.committed_bytes -= vmo.allocated_bytes;
    vmos_with_children.insert(vmo.parent_koid);
  }

  // Pass 3: Account child VMO allocations as fully "committed" ("add back" the
  // bytes subtracted in pass 2).
  for (auto& [vmo_koid, vmo] : vmo_data) {
    bool vmoHasChild = vmos_with_children.count(vmo_koid) == 1;

    // Leaf VMOs are VMOs that have a parent and no children.
    if (vmo.parent_koid != 0 && !vmoHasChild) {
      vmo.committed_bytes = vmo.allocated_bytes;
    }
  }

  // Can be expensive, so log only if asked.
  if (FX_VLOG_IS_ON(2)) {
    // Match the format of `fx shell mem --print`
    FX_VLOGS(2) << "fx shell mem --print equivalent:";
    for (auto& [vmo_koid, vmo] : vmo_data) {
      FX_VLOGS(2) << "V," << vmo_koid << "," << vmo.name
                  << "," << vmo.parent_koid
                  << "," << vmo.committed_bytes;
    }
  }

  return vmo_data;
}

void GatherVmos::UploadSamples(
    const std::unordered_set<zx_koid_t>& live_process_koids,
    const std::map<zx_koid_t, GatherVmos::Vmo>& vmo_data) {
  TRACE_DURATION("harvester", "GatherVmos::UploadSamples");

  TRACE_DURATION_BEGIN("harvester", "build process -> rooted map");
  // Map of {process koids -> {rooted memory name -> total committed}}.
  ProcessMap process_rooted_memory;
  for (zx_koid_t process_koid : processes_with_rooted_vmos_) {
    for (zx_koid_t vmo_koid : process_to_vmos_[process_koid]) {
      if (!VmoHasRootedAncestor(vmo_koid)) {
        continue;
      }

      const Vmo& vmo = vmo_data.at(vmo_koid);

      // Find index of the name in the VmoMap of rooted names.
      size_t i = std::distance(kRootedVmoNames.begin(),
                               std::find(kRootedVmoNames.begin(),
                                         kRootedVmoNames.end(),
                                         vmo.name));
      if (i >= kNumRootedVmos) {
        continue;
      }

      process_rooted_memory[process_koid][i] += vmo.committed_bytes;
    }
  }
  TRACE_DURATION_END("harvester", "build process -> rooted map");

  if (FX_VLOG_IS_ON(2)) {
    // Human readable sample bundle equivalent.
    for (auto& [process_koid, vmo_map] : process_rooted_memory) {
      FX_VLOGS(2) << "Process " << process_koid;
      for (size_t i = 0; i < kNumRootedVmos; ++i) {
        FX_VLOGS(2) << "* " << vmo_map[i] << " bytes for "
                    << kRootedVmoNames[i];
      }
    }
  }

  TRACE_DURATION_BEGIN("harvester", "AddIntSample");
  SampleBundle samples;
  for (zx_koid_t koid : live_process_koids) {
    const auto& vmo_map = process_rooted_memory[koid];

    for (size_t i = 0; i < kNumRootedVmos; ++i) {
      samples.AddIntSample("koid", koid, kRootedMemoryPaths[i], vmo_map[i]);
    }
  }
  TRACE_DURATION_END("harvester", "AddIntSample");

  TRACE_DURATION_BEGIN("harvester", "Upload");
  samples.Upload(DockyardPtr());
  TRACE_DURATION_END("harvester", "Upload");
}

void GatherVmos::GatherDeviceProperties() {}

void GatherVmos::Gather() {
  TRACE_DURATION("harvester", "GatherVmos::Gather");
  // NOTE: g_slow_data_task_tree is gathered in GatherChannels::Gather().

  // First, gather VMOs.
  std::vector<zx_koid_t> new_vmos;
  std::unordered_set<zx_koid_t> live_process_koids;
  DoSparseVmoUpdate(task_tree_, new_vmos, live_process_koids);

  // Given a current list of live processes, prune any dead process VMO lists.
  CleanProcessToVmos(live_process_koids);

  // Next, build the rooted_vmo_descendants cache.
  BuildRootedVmoDescendants(new_vmos);

  // Requires rooted_vmo_descendants_.
  BuildProcessesWithRootedVmos();

  // Now we're ready to build VMO data.
  std::map<zx_koid_t, Vmo> vmo_data = BuildVmoData();

  // Upload everything to the dockyard for the frontend.
  UploadSamples(live_process_koids, vmo_data);

  // After everything else, cache live_process_koids for next iteration.
  last_seen_processes_ = std::move(live_process_koids);
}

}  // namespace harvester

