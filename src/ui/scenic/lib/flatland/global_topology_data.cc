// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_topology_data.h"

#include "src/lib/fxl/logging.h"

namespace {
struct pair_hash {
  size_t operator()(const std::pair<flatland::TransformHandle, uint64_t>& p) const noexcept {
    return std::hash<flatland::TransformHandle>{}(p.first) ^ std::hash<uint64_t>{}(p.second);
  }
};
}  // namespace

namespace flatland {

// static
GlobalTopologyData GlobalTopologyData::ComputeGlobalTopologyData(
    const UberStruct::InstanceMap& uber_structs, const LinkSystem::LinkTopologyMap& links,
    TransformHandle::InstanceId link_instance_id, TransformHandle root) {
  // There should never be an UberStruct for the |link_instance_id|.
  FXL_DCHECK(uber_structs.find(link_instance_id) == uber_structs.end());

  // This is a stack of vector "iterators". We store the raw index, instead of an iterator, so that
  // we can do index comparisons.
  std::vector<std::pair<const TransformGraph::TopologyVector&, /*local_index=*/size_t>>
      vector_stack;
  // This is a stack of global parent indices and the number of children left to process for that
  // parent.
  std::vector<std::pair</*parent_index=*/uint64_t, /*children_left=*/uint64_t>> parent_counts;
  TransformGraph::TopologyVector retval;
  std::unordered_set<TransformHandle> live_transforms;

  // If we don't have the root in the map, the topology will be empty.
  auto root_iter = uber_structs.find(root.GetInstanceId());
  if (root_iter != uber_structs.cend()) {
    vector_stack.emplace_back(root_iter->second->local_topology, 0);
  }

  while (!vector_stack.empty()) {
    auto& [vector, iterator_index] = vector_stack.back();

    // If we are finished with a vector, pop back to the previous vector.
    if (iterator_index >= vector.size()) {
      vector_stack.pop_back();
      continue;
    }

    auto current_entry = vector[iterator_index];
    ++iterator_index;

    // Mark that a child has been processed for the latest parent.
    if (!parent_counts.empty()) {
      --parent_counts.back().second;
    }

    // If we are processing a link transform, find the other end of the link (if it exists).
    if (current_entry.handle.GetInstanceId() == link_instance_id) {
      // Decrement the parent's child count until the link is successfully resolved. An unresolved
      // link effectively means the parent had one fewer child.
      FXL_DCHECK(!parent_counts.empty());
      auto& parent_entry = retval[parent_counts.back().first];
      --parent_entry.child_count;

      // If the link doesn't exist, skip the link handle.
      auto link_iter = links.find(current_entry.handle);
      if (link_iter == links.end()) {
        continue;
      }

      // If the link exists but doesn't have an UberStruct, skip the link handle.
      auto new_vector_iter = uber_structs.find(link_iter->second.GetInstanceId());
      if (new_vector_iter == uber_structs.end()) {
        continue;
      }

      // If the link exists and has an UberStruct but does not begin with the specified handle, skip
      // the new topology. This can occur if a new UberStruct has not been registered for the
      // corresponding instance ID but the link to it has resolved.
      const auto& new_vector = new_vector_iter->second->local_topology;
      FXL_DCHECK(!new_vector.empty());
      auto new_entry = new_vector[0];

      if (new_entry.handle != link_iter->second) {
        continue;
      }

      // Thanks to one-view-per-session semantics, we should never cycle through the
      // topological vectors, so we don't need to handle cycles. We DCHECK here just to be sure.
      FXL_DCHECK(
          std::find_if(vector_stack.cbegin(), vector_stack.cend(),
                       [&](std::pair<const TransformGraph::TopologyVector&, uint64_t> entry) {
                         return entry.first == new_vector;
                       }) == vector_stack.cend());

      // At this point, the link is resolved. This means the link did actually result in the parent
      // having an additional child, but that child needs to be processed, so the stack of remaining
      // children to process for each parent needs to be increment as well.
      ++parent_entry.child_count;
      ++parent_counts.back().second;

      vector_stack.emplace_back(new_vector, 0);
      continue;
    }

    // Push the current transform and update the "iterator".
    uint64_t new_parent_index = retval.size();
    
    retval.push_back(current_entry);
    live_transforms.insert(current_entry.handle);

    // If this entry was the last child for the previous parent, pop that off the stack.
    if (!parent_counts.empty() && parent_counts.back().second == 0) {
      parent_counts.pop_back();
    }

    // If this entry has children, push it onto the parent stack.
    if (current_entry.child_count != 0) {
      parent_counts.emplace_back(new_parent_index, current_entry.child_count);
    }
  }

  // Validates that every child of every parent was processed. If the last handle processed was an
  // unresolved link handle, its parent will be the only thing left on the stack with 0 children to
  // avoid extra unnecessary cleanup logic.
  FXL_DCHECK(parent_counts.empty() ||
             (parent_counts.size() == 1 && parent_counts.back().second == 0));

  return {.topology_vector = std::move(retval), .live_handles = std::move(live_transforms)};
}

}  // namespace flatland
