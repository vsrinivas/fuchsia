// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_VMOS_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_VMOS_H_

#include <array>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "gather_category.h"
#include "os.h"
#include "union_find.h"

namespace harvester {

class SampleBundle;

const size_t kNumRootedVmos = 4;

template <typename T>
using VmoMap = std::array<T, kNumRootedVmos>;
using MemType = int64_t;
using ProcessMap = std::unordered_map<zx_koid_t, VmoMap<MemType>>;

// Gather detailed VMO information from the kernel.
class GatherVmos : public GatherCategory {
 public:
  // Public-facing Vmo structure (instead of zx_info_vmo_t).
  struct Vmo {
    Vmo() = default;

    explicit Vmo(const zx_info_vmo_t& raw_vmo)
        : parent_koid(raw_vmo.parent_koid),
          committed_bytes(raw_vmo.committed_bytes),
          allocated_bytes(raw_vmo.size_bytes) {
      memcpy(name, raw_vmo.name, sizeof(name));
    }

    zx_koid_t parent_koid;
    // fx mem uses uint64_t here resulting in underflow.
    MemType committed_bytes;
    MemType allocated_bytes;
    char name[ZX_MAX_NAME_LEN];
  };

  GatherVmos(zx_handle_t root_resource,
             harvester::DockyardProxy* dockyard_proxy,
             harvester::TaskTree& task_tree,
             OS* os)
      : GatherCategory(root_resource, dockyard_proxy), task_tree_(task_tree),
        os_(os) {}

  // GatherCategory.
  void GatherDeviceProperties() override;
  void Gather() override;

 private:
  GatherVmos() = delete;

  // TaskTree used for VMO gathering.
  TaskTree& task_tree_;

  // Mockable proxy class for OS calls like zx_object_get_info().
  OS* os_;

  // Data kept between Gather() invocations, and updated by
  // DoSparseVmoUpdate().

  // Map of all vmos in the system. |zx_info_vmo_t| contains the koid and
  // parent_koid. Fetching VMO handles is not possible by design.
  std::unordered_map<zx_koid_t, zx_info_vmo_t> koids_to_vmos_;
  // Necessary to detect new processes (for immediate VMO scanning).
  std::unordered_set<zx_koid_t> last_seen_processes_;
  // Queue of processes we will scan sparsely.
  std::deque<TaskTree::Task> process_scan_queue_;
  // Map of process koids to their VMOs.
  std::unordered_map<zx_koid_t, std::unordered_set<zx_koid_t>> process_to_vmos_;
  // List of processes that should be scanned on every gather.
  std::unordered_set<zx_koid_t> processes_with_rooted_vmos_;
  // Set of VMOs that are related to rooted VMOs. Derived from |vmo_forest_|,
  // which is not fast enough for the number of queries we need to run.
  std::unordered_set<zx_koid_t> rooted_vmo_descendants_;
  // Map of rooted VMOs. This can't be a VmoMap because some VMOs occur
  // multiple times (e.g. Sysmem-external-heap).
  std::unordered_set<zx_koid_t> rooted_vmos_;
  // Disjoint sets of VMO koids.
  UnionFind<zx_koid_t> vmo_forest_;

  // Builds rooted_vmo_descendants_ set.
  void BuildRootedVmoDescendants(const std::vector<zx_koid_t>& new_vmos);

  // Builds the processes_with_rooted_vmos_ set.
  void BuildProcessesWithRootedVmos();

  // Builds the final rooted VMO data for dockyard.
  //
  // Returns a map of VMO koids to Vmo objects. This differs subtly from the
  // data in |koids_to_vmos_| to reflect a common usage pattern for rooted VMOs:
  // large allocations are often granted to services/processes that in turn
  // grant smaller slices to the processes that actually use the rooted memory.
  // BuildVmoData pushes those apparent allocations down from the services to
  // the children to more accurately reflect memory usage:
  //
  //      Sysmem-contig-core
  //              |
  //              v
  // <service-allocating-contig-mem>
  //        |           |
  //        v           v
  //   <process-1>  <process-2>
  //
  // In this case, our accounting of VMO memory usage will not count the
  // intermediate service as "owning" rooted memory that it has already
  // allocated to process 1 or process 2.
  std::map<zx_koid_t, Vmo> BuildVmoData();

  // Removes dead processes from process_to_vmos_.
  void CleanProcessToVmos(
      const std::unordered_set<zx_koid_t>& live_process_koids);

  // Updates *some of* the VMO lists in koids_to_vmos_ and process_to_vmos_.
  //
  // The criteria for whether or not to update is described in detail in the
  // function.
  //
  // Returns:
  // * A vector of previously unseen VMO koids for more efficient updates.
  // * An unordered set of the current live process koids.
  void DoSparseVmoUpdate(const TaskTree& task_tree,
                         std::vector<zx_koid_t>& new_vmos,
                         std::unordered_set<zx_koid_t>& live_process_koids);

  // Returns true if the VMO is descended from a rooted VMO.
  bool VmoHasRootedAncestor(zx_koid_t vmo_koid);

  // Returns the koid of the rooted ancestor VMO if one exists, zero if not.
  zx_koid_t GetRootedAncestorKoid(zx_koid_t child_vmo_koid);

  // Creates and stores handles to all VMOs belonging to |parent_process|.
  //
  // New VMOs are added to the |new_vmos| vector.
  void GatherVmosForProcess(zx_handle_t process, zx_koid_t process_koid,
                            std::vector<zx_koid_t>& new_vmos);

  // Uploads samples to dockyard.
  void UploadSamples(const std::unordered_set<zx_koid_t>& live_process_koids,
                     std::map<zx_koid_t, Vmo>& vmo_data);
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_VMOS_H_

