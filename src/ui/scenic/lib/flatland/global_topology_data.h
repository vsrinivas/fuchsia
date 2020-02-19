// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_TOPOLOGY_DATA_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_TOPOLOGY_DATA_H_

#include <unordered_set>

#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/transform_graph.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct.h"

namespace flatland {

struct GlobalTopologyData {
  // The global topology vector, which may contain TransformHandles from multiple TransformGraphs.
  // This vector will never contain TransformHandles authored by the LinkSystem.
  TransformGraph::TopologyVector topology_vector;

  // The set of TransformHandles in the topology vector (provided for convenience).
  std::unordered_set<TransformHandle> live_handles;

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
                                                      const LinkSystem::LinkTopologyMap& links,
                                                      TransformHandle::InstanceId link_instance_id,
                                                      TransformHandle root);
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_TOPOLOGY_DATA_H_
