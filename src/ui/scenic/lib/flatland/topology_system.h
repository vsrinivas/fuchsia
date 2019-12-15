// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_TOPOLOGY_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_TOPOLOGY_SYSTEM_H_

#include <mutex>
#include <unordered_map>

#include "src/ui/scenic/lib/flatland/transform_graph.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"

namespace flatland {

// A system for managing TransformGraph construction, and cross-graph traversal. All functions are
// thread safe. The intent is for separate worker threads to own TransformGraphs, compute topology
// vectors in their local threads, and then commit those vectors through this class in a concurrent
// manner.
class TopologySystem {
 public:
  TopologySystem() = default;

  // Creates an empty TransformGraph for this particular topology system. TransformHandles generated
  // by TransformGraphs are guaranteed to be unique, as long as all TransformGraphs were constructed
  // by the same TopologySystem instance.
  TransformGraph CreateGraph();

  // Computes the topologically sorted vector, consisting of all TransformHandles reachable from
  // |root|.
  //
  // |root| must be the first handle in a vector submitted through SetLocalTopology().
  //
  // TransformHandles will have their local topologies folded into the returned topological vector,
  // assuming that SetLocalTopology() has been called for that TransformHandle. If
  // SetLocalTopology() has not been called, the TransformHandle will still be present in the output
  // vector, but will not be expanded further.
  TransformGraph::TopologyVector ComputeGlobalTopologyVector(TransformHandle root);

  // Sets the topological vector for |sorted_transforms[0]|. Each TransformHandle may only have one
  // vector committed to the system at a time, calling SetLocalTopology() again will override the
  // existing TransformHandle's vector, if one exists.
  void SetLocalTopology(const TransformGraph::TopologyVector& sorted_transforms);

  // Clears a topological vector from the system. The TransformHandle passed in is the first
  // TransformHandle in the vector to be cleared.
  void ClearLocalTopology(TransformHandle transform);

  // For validating cleanup logic in tests.
  size_t GetSize();

 private:
  using TopologyMap = std::unordered_map<TransformHandle, TransformGraph::TopologyVector>;

  std::atomic<uint64_t> next_graph_id_ = 0;

  std::mutex map_mutex_;
  TopologyMap topology_map_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_TOPOLOGY_SYSTEM_H_
