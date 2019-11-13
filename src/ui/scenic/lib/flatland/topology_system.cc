// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/topology_system.h"

#include "src/lib/fxl/logging.h"

namespace {
struct pair_hash {
  size_t operator()(const std::pair<flatland::TransformHandle, uint64_t>& p) const noexcept {
    return std::hash<flatland::TransformHandle>{}(p.first) ^ std::hash<uint64_t>{}(p.second);
  }
};
}  // namespace

namespace flatland {

TransformGraph TopologySystem::CreateGraph() { return TransformGraph(next_graph_id_++); }

TransformGraph::TopologyVector TopologySystem::ComputeGlobalTopologyVector(TransformHandle root) {
  std::scoped_lock lock(map_mutex_);

  // This is a stack of vector "iterators". We store the raw index, instead of an iterator, so that
  // we can do index comparisons.
  std::vector<std::pair<const TransformGraph::TopologyVector&, /*local_index=*/uint64_t>>
      vector_stack;
  // This is a map from a TransformHandle and a local parent index, to the global parent index.
  std::unordered_map<std::pair</*transform_handle=*/TransformHandle, /*local_index=*/uint64_t>,
                     /*global_index=*/uint64_t, pair_hash>
      global_index_map;
  TransformGraph::TopologyVector retval;

  // We should have a graph map entry for our root graph.
  FXL_DCHECK(topology_map_.count(root));

  const TransformGraph::TopologyVector& initial_vector = topology_map_[root];

  // The root should be the first entry in the topological vector.
  FXL_DCHECK(!initial_vector.empty());
  FXL_DCHECK(initial_vector[0].handle == root);
  FXL_DCHECK(initial_vector[0].parent_index == 0);

  // Initialize the iterator stack.
  vector_stack.push_back({initial_vector, 0});

  while (!vector_stack.empty()) {
    auto& [vector, iterator_index] = vector_stack.back();

    // If we are finished with a vector, pop back to the previous vector.
    if (iterator_index >= vector.size()) {
      vector_stack.pop_back();
      continue;
    }

    auto current_root_handle = vector[0].handle;
    uint64_t new_global_index = retval.size();
    uint64_t local_parent_index = vector[iterator_index].parent_index;

    // Push the current transform and update the "iterator".
    global_index_map[{current_root_handle, iterator_index}] = new_global_index;
    uint64_t global_parent_index = global_index_map[{current_root_handle, local_parent_index}];
    auto current_transform = vector[iterator_index].handle;
    retval.push_back({current_transform, global_parent_index});
    ++iterator_index;

    auto new_graph_entry = topology_map_.find(current_transform);

    // If we don't have a local topology vector for the current transform, continue to the next.
    if (new_graph_entry == topology_map_.end() || iterator_index == 1) {
      continue;
    }

    const auto& new_vector = new_graph_entry->second;

    // Thanks to one-view-per-session semantics, we should never cycle through the
    // topological vectors, so we don't need to handle cycles. We DCHECK here, just to be sure.
    FXL_DCHECK(std::find_if(vector_stack.cbegin(), vector_stack.cend(),
                            [&](std::pair<const TransformGraph::TopologyVector&, uint64_t> entry) {
                              return entry.first == new_vector;
                            }) == vector_stack.cend());

    FXL_DCHECK(!new_vector.empty());
    FXL_DCHECK(new_vector[0].handle == current_transform);
    FXL_DCHECK(new_vector[0].parent_index == 0);

    // Because the first element in the new vector is the same as the transform that got us here,
    // we skip to the second element.
    vector_stack.push_back({new_vector, 1});
    global_index_map[{current_transform, 0}] = new_global_index;
  }

  return retval;
}

void TopologySystem::SetLocalTopology(const TransformGraph::TopologyVector& sorted_transforms) {
  FXL_DCHECK(!sorted_transforms.empty());
  FXL_DCHECK(sorted_transforms[0].parent_index == 0);

  // Copy the data outside of the lock
  TransformGraph::TopologyVector copy = sorted_transforms;

  // Acquire the lock and update.
  {
    std::scoped_lock lock(map_mutex_);
    topology_map_[copy[0].handle] = std::move(copy);
  }
}

void TopologySystem::ClearLocalTopology(TransformHandle transform) {
  // Acquire the lock and update.
  {
    std::scoped_lock lock(map_mutex_);
    FXL_DCHECK(topology_map_.count(transform));
    topology_map_.erase(transform);
  }
}

}  // namespace flatland
