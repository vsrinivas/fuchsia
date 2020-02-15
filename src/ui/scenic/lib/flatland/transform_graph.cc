// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/transform_graph.h"

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/flatland/uber_struct.h"

namespace {
struct pair_hash {
  size_t operator()(const std::pair<flatland::TransformHandle, uint64_t>& p) const noexcept {
    return std::hash<flatland::TransformHandle>{}(p.first) ^ std::hash<uint64_t>{}(p.second);
  }
};
}  // namespace

namespace flatland {

TransformGraph::TransformGraph() : TransformGraph(0) {}

TransformGraph::TransformGraph(TransformHandle::InstanceId instance_id)
    : instance_id_(instance_id) {}

TransformHandle TransformGraph::CreateTransform() {
  FXL_DCHECK(is_valid_);
  TransformHandle retval(instance_id_, next_transform_id_++);
  FXL_DCHECK(!working_set_.count(retval));
  working_set_.insert(retval);
  live_set_.insert(retval);
  return retval;
}

bool TransformGraph::ReleaseTransform(TransformHandle handle) {
  FXL_DCHECK(is_valid_);
  auto iter = working_set_.find(handle);
  if (iter == working_set_.end()) {
    return false;
  }

  working_set_.erase(iter);
  return true;
}

bool TransformGraph::AddChild(TransformHandle parent, TransformHandle child) {
  FXL_DCHECK(is_valid_);
  FXL_DCHECK(working_set_.count(parent));

  auto [iter, end_iter] = children_.equal_range({parent, NORMAL});
  for (; iter != end_iter; ++iter) {
    if (iter->second == child) {
      return false;
    }
  }

  children_.insert({{parent, NORMAL}, child});
  return true;
}

bool TransformGraph::RemoveChild(TransformHandle parent, TransformHandle child) {
  FXL_DCHECK(is_valid_);
  FXL_DCHECK(working_set_.count(parent));

  auto [iter, end_iter] = children_.equal_range({parent, NORMAL});
  for (; iter != end_iter; ++iter) {
    if (iter->second == child) {
      children_.erase(iter);
      return true;
    }
  }

  return false;
}

void TransformGraph::ClearChildren(TransformHandle parent) {
  FXL_DCHECK(is_valid_);
  FXL_DCHECK(working_set_.count(parent));
  children_.erase({parent, NORMAL});
}

void TransformGraph::SetPriorityChild(TransformHandle parent, TransformHandle child) {
  FXL_DCHECK(is_valid_);
  FXL_DCHECK(working_set_.count(parent));

  children_.erase({parent, PRIORITY});
  children_.insert({{parent, PRIORITY}, child});
}

void TransformGraph::ClearPriorityChild(TransformHandle parent) {
  FXL_DCHECK(is_valid_);
  FXL_DCHECK(working_set_.count(parent));

  children_.erase({parent, PRIORITY});
}

void TransformGraph::ResetGraph(TransformHandle exception) {
  FXL_DCHECK(working_set_.count(exception));
  working_set_.clear();
  working_set_.insert(exception);
  live_set_.clear();
  children_.clear();
  is_valid_ = true;
}

TransformGraph::TopologyData TransformGraph::ComputeAndCleanup(TransformHandle start,
                                                               uint64_t max_iterations) {
  FXL_DCHECK(is_valid_);
  FXL_DCHECK(working_set_.count(start));

  TopologyData data;

  // Swap all the live nodes into the dead set, so we can pull them out as we visit them.
  std::swap(live_set_, data.dead_transforms);

  // Clone our children map. We will remove child links after we visit them, to avoid duplicate
  // work when traversing the entire working set of transforms.
  PriorityChildMap children_copy = children_;

  // Compute the topological set starting from the start transform.
  data.sorted_transforms =
      Traverse(start, children_copy, &data.cyclical_edges, max_iterations - data.iterations);
  data.iterations += data.sorted_transforms.size();
  for (auto [transform, parent_index] : data.sorted_transforms) {
    auto [start, end] = EqualRangeAllPriorities(children_copy, transform);
    if (start != children_copy.cend()) {
      children_copy.erase(start, end);
    }
    data.dead_transforms.erase(transform);
    live_set_.insert(transform);
  }

  // Compute the topological set starting from every working set transform, for cleanup purposes.
  for (auto transform : working_set_) {
    auto working_transforms =
        Traverse(transform, children_copy, &data.cyclical_edges, max_iterations - data.iterations);
    data.iterations += working_transforms.size();
    for (auto [transform, parent_index] : working_transforms) {
      auto [start, end] = EqualRangeAllPriorities(children_copy, transform);
      if (start != children_copy.cend()) {
        children_copy.erase(start, end);
      }
      data.dead_transforms.erase(transform);
      live_set_.insert(transform);
    }
  }

  // Cleanup child state for all dead nodes.
  for (auto transform : data.dead_transforms) {
    auto [start, end] = EqualRangeAllPriorities(children_, transform);
    if (start != children_.cend()) {
      children_.erase(start, end);
    }
  }

  if (data.iterations >= max_iterations) {
    is_valid_ = false;
  }

  return data;
}

TransformGraph::IteratorPair TransformGraph::EqualRangeAllPriorities(const PriorityChildMap& map,
                                                                     TransformHandle handle) {
  auto start = map.lower_bound({handle, PRIORITY});
  auto end = map.upper_bound({handle, NORMAL});
  FXL_DCHECK(std::distance(start, end) >= 0);
  return {start, end};
}

TransformGraph::TopologyVector TransformGraph::Traverse(TransformHandle start,
                                                        const PriorityChildMap& children,
                                                        ChildMap* cycles, uint64_t max_length) {
  TopologyVector retval;

  std::vector<IteratorPair> iterator_stack;
  std::vector<TransformHandle> ancestors;

  // Add the starting handle to the output, and initialize our state.
  retval.push_back({start, 0});
  iterator_stack.push_back(EqualRangeAllPriorities(children, start));
  ancestors.push_back(start);
  uint64_t parent_index = 0;

  // Iterate until we're done, or until we run out of space
  while (!iterator_stack.empty() && retval.size() < max_length) {
    auto& [child_iter, end_iter] = iterator_stack.back();

    // If we're at the end of this iterator, pop to the parent iterator.
    if (child_iter == end_iter) {
      iterator_stack.pop_back();
      ancestors.pop_back();
      FXL_DCHECK(parent_index < retval.size());
      parent_index = retval[parent_index].parent_index;
      continue;
    }

    const TransformHandle child = child_iter->second;
    // We increment the child iterator here, instead of at the end of the loop, because the new
    // child may cause the iterator_stack to mutate, invalidating the live references we've
    // captured.
    ++child_iter;

    // Search from the bottom of the stack (since it's more likely), looking for a cycle.
    if (std::find(ancestors.crbegin(), ancestors.crend(), child) != ancestors.crend()) {
      FXL_DCHECK(cycles);
      cycles->insert({retval[parent_index].handle, child});
    } else {
      // If the child is not part of a cycle, add it to the sorted list and update our state.
      int new_parent_index = retval.size();
      retval.push_back({child, parent_index});
      iterator_stack.push_back(EqualRangeAllPriorities(children, child));
      ancestors.push_back(child);
      parent_index = new_parent_index;
    }
  }

  return retval;
}

// static
TransformGraph::GlobalTopologyData TransformGraph::ComputeGlobalTopologyVector(
    const std::unordered_map<TransformHandle::InstanceId, std::shared_ptr<UberStruct>>&
        uber_structs,
    const std::unordered_map<TransformHandle, TransformHandle>& links,
    TransformHandle::InstanceId link_instance_id, TransformHandle root) {
  // There should never be an UberStruct for the |link_instance_id|.
  FXL_DCHECK(uber_structs.find(link_instance_id) == uber_structs.end());

  // This is a stack of vector "iterators". We store the raw index, instead of an iterator, so that
  // we can do index comparisons.
  std::vector<std::pair<const TransformGraph::TopologyVector&, /*local_index=*/uint64_t>>
      vector_stack;
  // This is a map from a TransformHandle and a local parent index, to the global parent index.
  std::unordered_map<std::pair</*transform_handle=*/TransformHandle, /*local_index=*/uint64_t>,
                     /*global_index=*/uint64_t, pair_hash>
      global_index_map;
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

    auto current_transform = vector[iterator_index].handle;
    auto current_root_handle = vector[0].handle;
    uint64_t local_parent_index = vector[iterator_index].parent_index;

    // If we are processing a link transform, find the other end of the link (if it exists).
    if (current_transform.GetInstanceId() == link_instance_id) {
      // Regardless of whether or not the link has resolved, the link node has been processed, so
      // advance beyond it.
      ++iterator_index;

      // If the link has resolved and the child topology is present, add the new topology to process
      // to the stack.
      auto link_iter = links.find(current_transform);
      if (link_iter != links.end()) {
        auto new_vector_iter = uber_structs.find(link_iter->second.GetInstanceId());
        if (new_vector_iter != uber_structs.end()) {
          const auto& new_vector = new_vector_iter->second->local_topology;
          auto new_root_transform = new_vector[0].handle;

          FXL_DCHECK(!new_vector.empty());
          FXL_DCHECK(new_vector[0].parent_index == 0);

          // Skip this new topology if it doesn't actually belong to the handle specified. This can
          // occur if a new UberStruct has not been registered for the corresponding instance ID,
          // but the link to it has resolved.
          if (new_root_transform != link_iter->second) {
            continue;
          }

          // Thanks to one-view-per-session semantics, we should never cycle through the
          // topological vectors, so we don't need to handle cycles. We DCHECK here just to be sure.
          FXL_DCHECK(
              std::find_if(vector_stack.cbegin(), vector_stack.cend(),
                           [&](std::pair<const TransformGraph::TopologyVector&, uint64_t> entry) {
                             return entry.first == new_vector;
                           }) == vector_stack.cend());

          // Push the root of the new vector. Its parent index will be the parent of the link
          // transform currently being processed. This must be done here because the new root's
          // parent is in a different topology vector (which would overcomplicate the logic below).
          uint64_t new_global_index = retval.size();
          global_index_map[{new_root_transform, 0}] = new_global_index;
          uint64_t global_parent_index =
              global_index_map[{current_root_handle, local_parent_index}];
          retval.push_back({new_root_transform, global_parent_index});
          live_transforms.insert(new_root_transform);

          // Skip the root of the new vector in the next iteration since it was processed above.
          vector_stack.emplace_back(new_vector, 1);
        }
      }
      continue;
    }

    // Push the current transform and update the "iterator".
    uint64_t new_global_index = retval.size();
    global_index_map[{current_root_handle, iterator_index}] = new_global_index;
    uint64_t global_parent_index = global_index_map[{current_root_handle, local_parent_index}];
    retval.push_back({current_transform, global_parent_index});
    live_transforms.insert(current_transform);
    ++iterator_index;
  }

  return {.topology_vector = std::move(retval), .live_handles = std::move(live_transforms)};
}

}  // namespace flatland
