// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_TOPOLOGY_DATA_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_TOPOLOGY_DATA_H_

#include <string>
#include <unordered_set>

#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace flatland {

// A vector of indices that reference global vectors, such as the global topology vector.
using GlobalIndexVector = std::vector<size_t>;

struct GlobalTopologyData {
  // The LinkSystem stores topology links as a key-value pair of TransformHandles. This type alias
  // is declared because while this map is created by the LinkSystem, it is only ever consumed
  // by ComputeGlobalTopologyData().
  // Link handle acts as the key and child view watcher handle as the value.
  using LinkTopologyMap = std::unordered_map<TransformHandle, TransformHandle>;

  // The list of transforms reachable from a particular root, sorted in topological (i.e.,
  // depth-first) order. This vector may contain TransformHandles from multiple TransformGraphs,
  // but will never contain TransformHandles authored by the LinkSystem.
  //
  // Unlike the TransformGraph::TopologyVector, this vector does not contain child counts or any
  // other information regarding parent-child relationships; that data is stored separately in the
  // GlobalTopologyData.
  using TopologyVector = std::vector<TransformHandle>;
  TopologyVector topology_vector;

  // The list of direct child counts for each entry in the |topology_vector|.
  using ChildCountVector = std::vector<uint64_t>;
  ChildCountVector child_counts;

  // The list of parent indices for each entry in the |topology_vector|. The first entry will
  // always be zero to indicate that the first TransformHandle has no parent.
  using ParentIndexVector = std::vector<size_t>;
  ParentIndexVector parent_indices;

  // The set of TransformHandles in the |topology_vector| (provided for convenience).
  std::unordered_set<TransformHandle> live_handles;

  // ViewRef for each root TransformHandle. nullptr for anonymous Views.
  using ViewRefMap =
      std::unordered_map<TransformHandle, std::shared_ptr<const fuchsia::ui::views::ViewRef>>;
  ViewRefMap view_refs;

  // Map of TransformHandle to its local root TransformHandle. Needed for hit testing.
  std::unordered_map<TransformHandle, TransformHandle> root_transforms;

  // HitRegions for each TransformHandle.
  using HitRegions =
      std::unordered_map<TransformHandle, std::vector<fuchsia::ui::composition::HitRegion>>;
  HitRegions hit_regions;

  // Debug name for each transform handle, if present.
  std::unordered_map<TransformHandle, std::string> debug_names;

  // TransformClipRegion for each transform handle.
  std::unordered_map<TransformHandle, TransformClipRegion> clip_regions;

  // Computes the GlobalTopologyData consisting of all TransformHandles reachable from |root|.
  //
  // |root.GetInstanceId()| must be a key in |uber_structs|, and |root| must also be the first
  // TransformHandle in the topology vector of the UberStruct at that key.
  //
  // When the function encounters a TransformHandle whose instance ID is the |link_instance_id|,
  // it will search for that handle in the |links| map. If a value is found, that value is treated
  // as the root transform for a new local topology. If this new root transform has an entry in
  // |uber_structs| AND the first entry of that UberStruct's topology vector matches the new root
  // transform, then the new local topology is folded into the return topological vector. If either
  // of the aforementioned conditions is false, the TransformHandle on the other end of the link
  // will not be included.
  //
  // TransformHandles with the |link_instance_id| are never included in the final topology,
  // regardless of whether or not the link resolves.
  static GlobalTopologyData ComputeGlobalTopologyData(const UberStruct::InstanceMap& uber_structs,
                                                      const LinkTopologyMap& links,
                                                      TransformHandle::InstanceId link_instance_id,
                                                      TransformHandle root);

  static view_tree::SubtreeSnapshot GenerateViewTreeSnapshot(
      const GlobalTopologyData& data, const std::vector<TransformClipRegion>& global_clip_regions,
      const std::vector<glm::mat3>& global_matrix_vector,
      // Acquired from |LinkSystem::GetLinkChildToParentTransformMap|. Used to get the
      // TransformHandle of the parent end of a Link using the child's TransformHandle, in order to
      // fetch its clip region from |global_clip_regions|.
      const std::unordered_map<TransformHandle, TransformHandle>&
          link_child_to_parent_transform_map);
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_TOPOLOGY_DATA_H_
