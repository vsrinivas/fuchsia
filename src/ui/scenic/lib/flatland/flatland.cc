// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland.h"

#include "src/lib/fxl/logging.h"

namespace flatland {

// These macros provide convenient boilerplate, implementing delayed execution of commands until
// Present() is called.
#define BEGIN_PENDING_OPERATION() \
  pending_operations_.push_back([=]() {
#define END_PENDING_OPERATION() \
  });

void Flatland::Present(PresentCallback callback) {
  bool success = true;

  // TODO(36161): Don't execute operations until the (yet to be added) acquire fences have been
  // reached.
  for (auto& operation : pending_operations_) {
    if (!operation()) {
      success = false;
      break;
    }
  }

  pending_operations_.clear();

  topological_data_ = TopologicalData(transforms_, children_);

  // TODO(36166): Once the 2D scene graph is externalized, don't commit changes if a cycle is
  // detected. Instead, kill the channel and remove the sub-graph from the global graph.
  success &= topological_data_.cyclical_nodes().empty();

  // Clean up dead objects.
  for (auto iter = children_.begin(); iter != children_.end();) {
    if (!topological_data_.live_nodes().count(iter->first)) {
      iter = children_.erase(iter);
    } else {
      ++iter;
    }
  }

  fuchsia::ui::scenic::internal::Flatland_Present_Result result;
  if (success) {
    // TODO(36161): Once present operations can be pipelined, this variable will change state based
    // on the number of outstanding Present calls. Until then, this call is synchronous, and we can
    // always return 1 as the number of remaining presents.
    result.set_response({num_presents_remaining_});
  } else {
    result.set_err(fuchsia::ui::scenic::internal::Error::BAD_OPERATION);
  }

  callback(std::move(result));
}

void Flatland::ClearGraph() {
  BEGIN_PENDING_OPERATION();

  transforms_.clear();
  children_.clear();
  topological_data_ = TopologicalData();
  return true;

  END_PENDING_OPERATION();
}

void Flatland::CreateTransform(TransformId transform_id) {
  BEGIN_PENDING_OPERATION();

  // We store the global id of the root transform in the transform map under ID 0, since ID 0 is
  // invalid for user-generated transforms. This allows a single submission to the topological
  // sorter, instead of sending both the transform map, and the single additional root index.
  static_assert(kRootId == kInvalidId,
                "The invalid ID is reserved for placing the root transform in the transform map.");

  if (transform_id == kInvalidId) {
    FXL_LOG(ERROR) << "CreateTransform called with transform_id 0";
    return false;
  }

  if (transforms_.count(transform_id)) {
    FXL_LOG(ERROR) << "CreateTransform called with pre-existing transform_id " << transform_id;
    return false;
  }

  GlobalHandle handle{next_global_id_++};

  transforms_.insert({transform_id, handle});
  return true;

  END_PENDING_OPERATION();
}

void Flatland::AddChild(TransformId parent_transform_id, TransformId child_transform_id) {
  BEGIN_PENDING_OPERATION();

  if (parent_transform_id == kInvalidId || child_transform_id == kInvalidId) {
    FXL_LOG(ERROR) << "AddChild called with transform_id zero";
    return false;
  }

  auto parent_global_kv = transforms_.find(parent_transform_id);
  auto child_global_kv = transforms_.find(child_transform_id);

  if (parent_global_kv == transforms_.end()) {
    FXL_LOG(ERROR) << "AddChild failed, parent_transform_id " << parent_transform_id
                   << " not found";
    return false;
  }

  if (child_global_kv == transforms_.end()) {
    FXL_LOG(ERROR) << "AddChild failed, child_transform_id " << child_transform_id << " not found";
    return false;
  }

  const auto& parent_global_id = parent_global_kv->second;
  const auto& child_global_id = child_global_kv->second;

  auto [key_iter, end_iter] = children_.equal_range(parent_global_id);
  for (; key_iter != end_iter; ++key_iter) {
    if (key_iter->second == child_global_id) {
      FXL_LOG(ERROR) << "AddChild failed, link already exists between parent "
                     << parent_transform_id << " and child " << child_transform_id;
      return false;
    }
  }

  children_.insert(end_iter, {parent_global_kv->second, child_global_kv->second});
  return true;
  END_PENDING_OPERATION();
}

void Flatland::RemoveChild(TransformId parent_transform_id, TransformId child_transform_id) {
  BEGIN_PENDING_OPERATION();

  if (parent_transform_id == kInvalidId || child_transform_id == kInvalidId) {
    FXL_LOG(ERROR) << "RemoveChild failed, transform_id " << parent_transform_id << " not found";
    return false;
  }

  auto parent_global_kv = transforms_.find(parent_transform_id);
  auto child_global_kv = transforms_.find(child_transform_id);

  if (parent_global_kv == transforms_.end()) {
    FXL_LOG(ERROR) << "RemoveChild failed, parent_transform_id " << parent_transform_id
                   << " not found";
    return false;
  }

  if (child_global_kv == transforms_.end()) {
    FXL_LOG(ERROR) << "RemoveChild failed, child_transform_id " << child_transform_id
                   << " not found";
    return false;
  }

  const auto& parent_global_id = parent_global_kv->second;
  const auto& child_global_id = child_global_kv->second;

  auto [iter, end_iter] = children_.equal_range(parent_global_id);
  for (; iter != end_iter; ++iter) {
    if (iter->second == child_global_id) {
      children_.erase(iter);
      return true;
    }
  }

  FXL_LOG(ERROR) << "RemoveChild failed, link between parent " << parent_transform_id
                 << " and child " << child_transform_id << " not found";
  return false;

  END_PENDING_OPERATION();
}

void Flatland::SetRootTransform(TransformId transform_id) {
  BEGIN_PENDING_OPERATION();

  // The root transform is stored in the TransformMap, where all the mappings from user-generated
  // IDs to global IDs reside. Since the user is not allowed to use zero as a user-generated ID,
  // we store the root's global ID under that special key.
  //
  //  This way, the root is kept alive even if the client releases that particular non-zero user
  //  id. This also makes it possible to submit all live nodes to the topological sorter in a
  //  coherent way.

  // SetRootTransform(0) is special -- it clears the existing root transform.
  if (transform_id == kInvalidId) {
    transforms_.erase(kRootId);
    return true;
  }

  auto global_kv = transforms_.find(transform_id);
  if (global_kv == transforms_.end()) {
    FXL_LOG(ERROR) << "SetRootTransform failed, transform_id " << transform_id << " not found";
    return false;
  }

  transforms_.insert({kRootId, global_kv->second});
  return true;

  END_PENDING_OPERATION();
}

void Flatland::ReleaseTransform(TransformId transform_id) {
  BEGIN_PENDING_OPERATION();

  if (transform_id == kInvalidId) {
    FXL_LOG(ERROR) << "ReleaseTransform called with transform_id zero";
    return false;
  }

  bool erased = transforms_.erase(transform_id) != 0;

  if (!erased)
    FXL_LOG(ERROR) << "ReleaseTransform failed, transform_id " << transform_id << " not found";

  return erased;
  END_PENDING_OPERATION();
}

Flatland::TopologicalData::TopologicalData(const TransformMap& initial_nodes,
                                           const ChildMap& edges) {
  for (auto [id, global_id] : initial_nodes) {
    // Skip the node if we've already visited it.
    if (live_nodes_.count(global_id)) {
      continue;
    }

    // Insert this node, and all children, into the sorted data.
    Traverse(global_id, edges);
  }

  // If we have a root transform, it better be the first element in the topological sort.
  if (initial_nodes.count(kRootId)) {
    FXL_DCHECK(sorted_nodes_[0].first == initial_nodes.at(kRootId));
  }
}

void Flatland::TopologicalData::Traverse(GlobalHandle start, const ChildMap& edges) {
  std::vector<std::pair<ChildMap::const_iterator, ChildMap::const_iterator>> iterator_stack;
  std::vector<GlobalHandle> ancestors;

  uint64_t current_parent_index = sorted_nodes_.size();
  sorted_nodes_.push_back({start, current_parent_index});
  live_nodes_.insert(start);
  iterator_stack.push_back(edges.equal_range(start));
  ancestors.push_back(start);

  while (!iterator_stack.empty()) {
    auto& [child_iter, end_iter] = iterator_stack.back();

    if (child_iter == end_iter) {
      iterator_stack.pop_back();
      ancestors.pop_back();
      FXL_DCHECK(current_parent_index < sorted_nodes_.size());
      current_parent_index = sorted_nodes_[current_parent_index].second;
      continue;
    }

    const GlobalHandle child = child_iter->second;
    ++child_iter;

    // Search from the bottom of the stack (since it's more likely), looking for a cycle.
    if (std::find(ancestors.crbegin(), ancestors.crend(), child) != ancestors.crend()) {
      cyclical_nodes_.insert(child);
    } else {
      int new_parent_index = sorted_nodes_.size();
      sorted_nodes_.push_back({child, current_parent_index});
      live_nodes_.insert(child);
      iterator_stack.push_back(edges.equal_range(child));
      ancestors.push_back(child);
      current_parent_index = new_parent_index;
    }
  }
}

}  // namespace flatland
