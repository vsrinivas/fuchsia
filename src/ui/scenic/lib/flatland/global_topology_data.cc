// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_topology_data.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/flatland/flatland.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/lib/utils/logging.h"

#include <glm/gtc/type_ptr.hpp>

using flatland::GlobalTopologyData;
using flatland::TransformClipRegion;
using flatland::TransformHandle;

namespace {

struct pair_hash {
  size_t operator()(const std::pair<flatland::TransformHandle, uint64_t>& p) const noexcept {
    return std::hash<flatland::TransformHandle>{}(p.first) ^ std::hash<uint64_t>{}(p.second);
  }
};

std::optional<zx_koid_t> GetViewRefKoid(
    const flatland::TransformHandle& handle,
    const std::unordered_map<flatland::TransformHandle,
                             std::shared_ptr<const fuchsia::ui::views::ViewRef>>& view_ref_map) {
  const auto kv = view_ref_map.find(handle);
  if (kv == view_ref_map.end() || kv->second == nullptr) {
    return std::nullopt;
  }

  return utils::ExtractKoid(*kv->second);
}

std::optional<size_t> GetViewRefIndex(
    zx_koid_t view_ref_koid, const std::vector<flatland::TransformHandle>& transforms,
    const std::unordered_map<flatland::TransformHandle,
                             std::shared_ptr<const fuchsia::ui::views::ViewRef>>& view_refs) {
  for (const auto& [transform, view_ref] : view_refs) {
    if (view_ref != nullptr && view_ref_koid == utils::ExtractKoid(*view_ref)) {
      // Found |view_ref_koid|, now calculate the index of its root transform in |transforms|.
      for (size_t start = 0; start < transforms.size(); ++start) {
        if (transforms[start] == transform) {
          return start;
        }
      }
      FX_DCHECK(false) << "view ref root transform not in transforms vector";
    }
  }

  // |view_ref_koid| was not found.
  return std::nullopt;
}

// Returns the last index (exclusive) of the subtree rooted at |start|.
//
// Prerequisite: |start| was returned from `GetViewRefIndex()`
size_t GetSubtreeEndIndex(size_t start, const std::vector<flatland::TransformHandle>& transforms,
                          const std::vector<size_t>& parent_indices) {
  FX_DCHECK(start < transforms.size()) << "precondition";

  // We need to be careful about the case where start == 0, since in that case hitting the global
  // root and hitting start are identical. It is simpler to handle this case explicitly, and then in
  // the loop below we have this additional guarantee.
  if (start == 0) {
    return transforms.size();
  }

  // |end| is an exclusive index.
  size_t end = start + 1;

  // This is an O(n logn) op. We can make it O(n) if performance needs dictate.
  while (end < transforms.size()) {
    // Do an ancestor check to see if the current transform is a descendant of |start_node|.
    size_t cur_idx = end;
    while (cur_idx != start && cur_idx != 0) {
      cur_idx = parent_indices[cur_idx];
    }

    // We have ran up the ancestor tree until we hit the root of the entire tree - exit.
    if (cur_idx == 0) {
      break;
    }

    ++end;
  }

  return end;
}
// Converts a 3x3 (2D) matrix to its 4x4 (3D) analog.
// xx, xy, yx, yy represent scale/rotation, T for translation.
//
// xx xy Tx
// yx yy TY
// 00 00 01
//
// ...becomes...
//
// xx xy 00 TX
// yx yy 00 TY
// 00 00 01 00
// 00 00 00 01
glm::mat4 Convert2DTransformTo3D(glm::mat3 in_matrix) {
  // Construct identity matrix.
  glm::mat4 out_matrix = glm::mat4(1.f);
  // Assign the rotation and scale values to the 2x2 submatrix.
  out_matrix[0][0] = in_matrix[0][0];
  out_matrix[0][1] = in_matrix[0][1];
  out_matrix[1][0] = in_matrix[1][0];
  out_matrix[1][1] = in_matrix[1][1];
  // Assign the translation values to the final column.
  out_matrix[3][0] = in_matrix[2][0];
  out_matrix[3][1] = in_matrix[2][1];

  return out_matrix;
}

// Easier-to-read input data to HitTest() below.
struct HitTestingData {
  const GlobalTopologyData::TopologyVector transforms;
  const GlobalTopologyData::ParentIndexVector parent_indices;
  const std::unordered_map<flatland::TransformHandle, flatland::TransformHandle> root_transforms;
  const GlobalTopologyData::ViewRefMap view_refs;
  const GlobalTopologyData::HitRegions hit_regions;
  const std::vector<flatland::TransformClipRegion> global_clip_regions;
};

view_tree::SubtreeHitTestResult HitTest(const HitTestingData& data, zx_koid_t start_node,
                                        glm::vec2 world_point, bool is_semantic_hit_test) {
  const auto& [transforms, parent_indices, root_transforms, view_refs, hit_regions,
               global_clip_regions] = data;
  FX_DCHECK(transforms.size() == parent_indices.size());
  FX_DCHECK(transforms.size() == global_clip_regions.size());

  size_t start = 0, end = 0;
  if (auto result = GetViewRefIndex(start_node, transforms, view_refs)) {
    start = *result;
    end = GetSubtreeEndIndex(start, transforms, parent_indices);
  } else {
    // Start node not in view tree.
    return view_tree::SubtreeHitTestResult{};
  }

  FX_DCHECK(0 <= start && start < end && end <= transforms.size());

  const auto x = world_point[0];
  const auto y = world_point[1];

  std::vector<zx_koid_t> hits = {};

  for (size_t i = start; i < end; ++i) {
    const auto& transform = transforms[i];
    FX_DCHECK(root_transforms.find(transform) != root_transforms.end());
    const auto& root_transform = root_transforms.at(transform);

    const auto clip_region = utils::ConvertRectToRectF(global_clip_regions[i]);

    // Skip anonymous views.
    if (const auto local_root = view_refs.find(root_transform);
        local_root != view_refs.end() && local_root->second != nullptr) {
      const auto& view_ref = *local_root->second;
      // Skip views without hit regions.
      if (const auto hit_region_vec = hit_regions.find(transform);
          hit_region_vec != hit_regions.end()) {
        for (const auto& region : hit_region_vec->second) {
          const auto rect = region.region;
          const bool semantically_invisible =
              region.hit_test ==
              fuchsia::ui::composition::HitTestInteraction::SEMANTICALLY_INVISIBLE;

          // Deliver a hit in all cases except for when it is a semantic hit test and the region
          // is semantically invisible.
          if (is_semantic_hit_test && semantically_invisible) {
            continue;
          }

          // Instead of clipping the hit region with the clip region, simply check if the hit
          // point is in both.
          if (utils::RectFContainsPoint(rect, x, y) &&
              utils::RectFContainsPoint(clip_region, x, y)) {
            hits.push_back(utils::ExtractKoid(view_ref));
            break;
          }
        }
      }
    }
  }

  std::reverse(hits.begin(), hits.end());
  return view_tree::SubtreeHitTestResult{.hits = hits};
}

// Returns whether the transform at |index| has an anonymous ancestor.
bool HasAnonymousAncestor(const size_t index, const size_t root_index,
                          const GlobalTopologyData::ViewRefMap& view_refs,
                          const GlobalTopologyData::TopologyVector& topology_vector,
                          const GlobalTopologyData::ParentIndexVector& parent_indices) {
  if (index == root_index) {
    return false;
  }

  size_t parent_index = parent_indices[index];
  while (parent_index != root_index) {
    // A transform that has an entry in the ViewRefMap is a view, but a nullptr entry is an
    // anonymous view.
    const auto parent_transform_handle = topology_vector[parent_index];
    const auto it = view_refs.find(parent_transform_handle);
    if (it != view_refs.end() && it->second == nullptr) {
      return true;
    }

    parent_index = parent_indices[parent_index];
  }

  return false;
}

// Returns the index and ViewRef koid first node in the topology with a ViewRef set.
// If none is found it returns std::nullopt, indicating an empty ViewTree.
std::optional<std::pair<size_t, zx_koid_t>> FindRoot(
    const GlobalTopologyData::TopologyVector& topology_vector,
    const GlobalTopologyData::ViewRefMap& view_refs) {
  for (size_t index = 0; index < topology_vector.size(); ++index) {
    // TODO(fxbug.dev/109352): Make sure the root view is not anonymous?
    if (const auto koid = GetViewRefKoid(topology_vector[index], view_refs)) {
      return std::make_pair(index, *koid);
    }
  }

  return std::nullopt;
}

// Finds the parent of the node at |index| by looking upwards until a View is found.
// Returns ZX_KOID INVALID if no valid parent is found. (The root has no parent)
zx_koid_t FindParentView(const size_t index, const zx_koid_t view_ref_koid, const zx_koid_t root,
                         const GlobalTopologyData::TopologyVector& topology_vector,
                         const GlobalTopologyData::ParentIndexVector& parent_indices,
                         const GlobalTopologyData::ViewRefMap& view_refs) {
  zx_koid_t parent_koid = ZX_KOID_INVALID;
  if (view_ref_koid != root) {
    size_t parent_index = parent_indices[index];
    while (view_refs.count(topology_vector[parent_index]) == 0) {
      parent_index = parent_indices[parent_index];
    }
    parent_koid = GetViewRefKoid(topology_vector[parent_index], view_refs).value();
  }
  return parent_koid;
}

// Returns the bounding box of |transform_handle| by findings the clip regions specified by its
// View's parent.
view_tree::BoundingBox ComputeBoundingBox(
    const TransformHandle transform_handle,
    const std::unordered_map<TransformHandle, TransformClipRegion>& clip_regions,
    const std::unordered_map<TransformHandle, TransformHandle>&
        link_child_to_parent_transform_map) {
  std::array<float, 2> max_bounds = {0, 0};
  if (const auto it = link_child_to_parent_transform_map.find(transform_handle);
      it != link_child_to_parent_transform_map.end()) {
    const TransformHandle parent_transform_handle = it->second;
    if (const auto clip_region_it = clip_regions.find(parent_transform_handle);
        clip_region_it != clip_regions.end()) {
      const auto [_x, _y, width, height] = clip_region_it->second;
      max_bounds = {static_cast<float>(width), static_cast<float>(height)};
    }
  }

  return {.min = {0, 0}, .max = max_bounds};
}

// Return value struct for ComputeViewTree().
struct ViewTreeData {
  std::unordered_map<zx_koid_t, view_tree::ViewNode> view_tree;
  std::unordered_set<TransformHandle> implicitly_anonymous_views;
};

// Computes and returns the ViewTree plus a list of implicit anonymous views (named views that are
// part of an anonymous subtree) based on GlobalTopologyData.
ViewTreeData ComputeViewTree(
    const zx_koid_t root, const size_t root_index,
    const GlobalTopologyData::TopologyVector& topology_vector,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const GlobalTopologyData::ViewRefMap& view_refs,
    const std::unordered_map<TransformHandle, std::string>& debug_names,
    const std::unordered_map<TransformHandle, TransformClipRegion>& global_clip_regions,
    const std::vector<glm::mat3>& global_matrix_vector,
    const std::unordered_map<TransformHandle, TransformHandle>&
        link_child_to_parent_transform_map) {
  ViewTreeData output;
  for (size_t i = root_index; i < topology_vector.size(); ++i) {
    const auto& transform_handle = topology_vector.at(i);
    const auto view_ref_it = view_refs.find(transform_handle);
    // Transforms without ViewRefs are not Views and can be skipped.
    if (view_ref_it == view_refs.end()) {
      continue;
    }

    const auto& view_ref = view_ref_it->second;
    // Anonymous views can be skipped.
    if (view_ref == nullptr) {
      continue;
    }

    // If any node in the ancestor chain is anonymous then the View should marked "unconnected".
    if (HasAnonymousAncestor(i, root_index, view_refs, topology_vector, parent_indices)) {
      output.implicitly_anonymous_views.emplace(transform_handle);
      continue;
    }

    std::string debug_name;
    if (auto debug_it = debug_names.find(transform_handle); debug_it != debug_names.end()) {
      debug_name = debug_it->second;
    }

    const zx_koid_t view_ref_koid = utils::ExtractKoid(*view_ref);
    const zx_koid_t parent_koid =
        FindParentView(i, view_ref_koid, root, topology_vector, parent_indices, view_refs);
    const view_tree::BoundingBox bounding_box = ComputeBoundingBox(
        transform_handle, global_clip_regions, link_child_to_parent_transform_map);

    output.view_tree.emplace(
        view_ref_koid, view_tree::ViewNode{.parent = parent_koid,
                                           .bounding_box = bounding_box,
                                           .local_from_world_transform = glm::inverse(
                                               Convert2DTransformTo3D(global_matrix_vector[i])),
                                           .view_ref = view_ref,
                                           .debug_name = debug_name});
  }

  // Fill in the children by deriving it from the parents of each node.
  for (const auto& [koid, view_node] : output.view_tree) {
    if (view_node.parent != ZX_KOID_INVALID) {
      output.view_tree.at(view_node.parent).children.emplace(koid);
    }
  }

  return output;
}

}  // namespace

namespace flatland {

// static
GlobalTopologyData GlobalTopologyData::ComputeGlobalTopologyData(
    const UberStruct::InstanceMap& uber_structs, const LinkTopologyMap& links,
    TransformHandle::InstanceId link_instance_id, TransformHandle root) {
  // There should never be an UberStruct for the |link_instance_id|.
  FX_DCHECK(uber_structs.find(link_instance_id) == uber_structs.end());

#ifdef USE_FLATLAND_VERBOSE_LOGGING
  {
    std::ostringstream str;
    str << "ComputeGlobalTopologyData(): Dumping UberStructs:\n";
    for (auto& kv : uber_structs) {
      str << *kv.second << "...................\n";
    }
    FLATLAND_VERBOSE_LOG << str.str();
  }
#endif

  // This is a stack of vector "iterators". We store the raw index, instead of an iterator, so that
  // we can do index comparisons.
  std::vector<std::pair<const TransformGraph::TopologyVector&, /*local_index=*/size_t>>
      vector_stack;
  // This is a stack of global parent indices and the number of children left to process for that
  // parent.
  struct ParentChildIterator {
    size_t parent_index = 0;
    size_t children_left = 0;
  };
  std::vector<ParentChildIterator> parent_counts;

  TopologyVector topology_vector;
  ChildCountVector child_counts;
  ParentIndexVector parent_indices;
  std::unordered_set<TransformHandle> live_transforms;
  ViewRefMap view_refs;
  std::unordered_map<TransformHandle, TransformHandle> root_transforms;
  std::unordered_map<TransformHandle, std::string> debug_names;
  std::unordered_map<TransformHandle, TransformClipRegion> clip_regions;

  // For the root of each local topology (i.e. the View), save the ViewRef, whether they're
  // currently attached to the scene or not.
  for (const auto& [_, uber_struct] : uber_structs) {
    if (!uber_struct->local_topology.empty()) {
      view_refs.emplace(uber_struct->local_topology[0].handle, uber_struct->view_ref);
    }
  }

  // If we don't have the root in the map, the topology will be empty.
  const auto root_uber_struct_kv = uber_structs.find(root.GetInstanceId());
  if (root_uber_struct_kv != uber_structs.cend()) {
    vector_stack.emplace_back(root_uber_struct_kv->second->local_topology, 0);
  }

  while (!vector_stack.empty()) {
    auto& [vector, iterator_index] = vector_stack.back();

    // If we are finished with a vector, pop back to the previous vector.
    if (iterator_index >= vector.size()) {
      FX_DCHECK(iterator_index == vector.size());
      vector_stack.pop_back();
      continue;
    }

    const auto& current_entry = vector[iterator_index];

    FLATLAND_VERBOSE_LOG << "GlobalTopologyData processing current_entry=" << current_entry.handle
                         << "  child-count: " << current_entry.child_count;
    ++iterator_index;

    // Mark that a child has been processed for the latest parent.
    if (!parent_counts.empty()) {
      FLATLAND_VERBOSE_LOG << "GlobalTopologyData       parent_counts size: "
                           << parent_counts.size()
                           << "  parent: " << topology_vector[parent_counts.back().parent_index]
                           << "  remaining-children: " << parent_counts.back().children_left;

      FX_DCHECK(parent_counts.back().children_left > 0);
      --parent_counts.back().children_left;
    } else {
      // Only expect to see this at the root of the topology, I think.  Is this worth putting a
      // permanent check in?
      FLATLAND_VERBOSE_LOG << "GlobalTopologyData       no parent";
    }

    // If we are processing a link transform, find the other end of the link (if it exists).
    if (current_entry.handle.GetInstanceId() == link_instance_id) {
      // Decrement the parent's child count until the link is successfully resolved. An unresolved
      // link effectively means the parent had one fewer child.
      FX_DCHECK(!parent_counts.empty());
      auto& parent_child_count = child_counts[parent_counts.back().parent_index];
      --parent_child_count;

      // If the link doesn't exist, skip the link handle.
      const auto link_kv = links.find(current_entry.handle);
      if (link_kv == links.end()) {
        FLATLAND_VERBOSE_LOG << "GlobalTopologyData link doesn't exist for handle "
                             << current_entry.handle << ", skipping ";

        if (parent_counts.back().children_left == 0) {
          parent_counts.pop_back();
        }

        continue;
      }
      const TransformHandle& link_transform = link_kv->second;

      // If the link exists but doesn't have an UberStruct, skip the link handle.
      const auto uber_struct_kv = uber_structs.find(link_transform.GetInstanceId());
      if (uber_struct_kv == uber_structs.end()) {
        FLATLAND_VERBOSE_LOG << "GlobalTopologyData link doesn't exist for instance_id "
                             << link_transform.GetInstanceId() << ", skipping";

        if (parent_counts.back().children_left == 0) {
          parent_counts.pop_back();
        }

        continue;
      }

      // If the link exists and has an UberStruct but does not begin with the specified handle, skip
      // the new topology. This can occur if a new UberStruct has not been registered for the
      // corresponding instance ID but the link to it has resolved.
      const auto& new_vector = uber_struct_kv->second->local_topology;
      FX_DCHECK(!new_vector.empty()) << "Valid UberStructs cannot have empty local_topology";
      if (new_vector[0].handle != link_transform) {
        FLATLAND_VERBOSE_LOG << "GlobalTopologyData link mismatch with "
                                "existing UberStruct ("
                             << new_vector[0].handle << " vs. " << link_transform << "), skipping";

        if (parent_counts.back().children_left == 0) {
          parent_counts.pop_back();
        }

        continue;
      }

      // Thanks to one-view-per-session semantics, we should never cycle through the
      // topological vectors, so we don't need to handle cycles. We DCHECK here just to be sure.
      FX_DCHECK(std::find_if(vector_stack.cbegin(), vector_stack.cend(),
                             [&](std::pair<const TransformGraph::TopologyVector&, uint64_t> entry) {
                               return entry.first == new_vector;
                             }) == vector_stack.cend());

      // At this point, the link is resolved. This means the link did actually result in the parent
      // having an additional child, but that child needs to be processed, so the stack of remaining
      // children to process for each parent needs to be increment as well.
      ++parent_child_count;
      ++parent_counts.back().children_left;

      vector_stack.emplace_back(new_vector, 0);
      continue;
    }

    // Push the current transform and update the "iterator".
    const size_t new_parent_index = topology_vector.size();
    topology_vector.push_back(current_entry.handle);
    // For each transform in the local topology, record its root.
    root_transforms.emplace(current_entry.handle, vector[0].handle);

    child_counts.push_back(current_entry.child_count);
    parent_indices.push_back(parent_counts.empty() ? 0 : parent_counts.back().parent_index);
    live_transforms.insert(current_entry.handle);

    // For the root of each local topology (i.e. the View), save the debug name if it is not empty.
    if (current_entry == vector[0] &&
        !uber_structs.at(current_entry.handle.GetInstanceId())->debug_name.empty()) {
      debug_names.emplace(current_entry.handle,
                          uber_structs.at(current_entry.handle.GetInstanceId())->debug_name);
    }

    // For each node in the local topology, save the TransformClipRegion of its child instances.
    for (auto& [child_handle, child_clip_region] :
         uber_structs.at(current_entry.handle.GetInstanceId())->local_clip_regions) {
      TransformClipRegion clip_region;
      fidl::Clone(child_clip_region, &clip_region);
      clip_regions.try_emplace(child_handle, std::move(clip_region));
    }

    // If this entry was the last child for the previous parent, pop that off the stack.
    if (!parent_counts.empty() && parent_counts.back().children_left == 0) {
      parent_counts.pop_back();
    }

    // If this entry has children, push it onto the parent stack.
    if (current_entry.child_count != 0) {
      parent_counts.push_back({new_parent_index, current_entry.child_count});
    }
  }

// Validates that every child of every parent was processed. If the last handle processed was an
// unresolved link handle, its parent will be the only thing left on the stack with 0 children to
// avoid extra unnecessary cleanup logic.
#ifndef NDEBUG
  if (parent_counts.size() > 1 ||
      (parent_counts.size() == 1 && parent_counts.back().children_left != 0)) {
    std::ostringstream str;
    str << "Error while generating GlobalTopologyData (failed parent_counts validation)\n"
        << "Dumping parent_counts vector:\n";
    for (size_t i = 0; i < parent_counts.size(); ++i) {
      str << "i: " << i << "  index: " << parent_counts[i].parent_index
          << "  parent: " << topology_vector[parent_counts[i].parent_index]
          << "  child-count: " << parent_counts[i].children_left << std::endl;
    }
    FX_LOGS(FATAL) << str.str();
  }
  FX_CHECK(parent_counts.empty() ||
           (parent_counts.size() == 1 && parent_counts.back().children_left == 0));
#endif

  return {.topology_vector = std::move(topology_vector),
          .child_counts = std::move(child_counts),
          .parent_indices = std::move(parent_indices),
          .live_handles = std::move(live_transforms),
          .view_refs = std::move(view_refs),
          .root_transforms = std::move(root_transforms),
          .debug_names = std::move(debug_names),
          .clip_regions = std::move(clip_regions)};
}

view_tree::SubtreeSnapshot GlobalTopologyData::GenerateViewTreeSnapshot(
    const GlobalTopologyData& data, const std::vector<TransformClipRegion>& global_clip_regions,
    const std::vector<glm::mat3>& global_matrix_vector,
    const std::unordered_map<TransformHandle, TransformHandle>&
        link_child_to_parent_transform_map) {
  const auto root_values = FindRoot(data.topology_vector, data.view_refs);
  if (!root_values.has_value()) {
    // No root -> Empty ViewTree.
    return {};
  }
  const auto [root_index, root_koid] = root_values.value();
  auto [view_tree, implicitly_anonymous_views] =
      ComputeViewTree(root_koid, root_index, data.topology_vector, data.parent_indices,
                      data.view_refs, data.debug_names, data.clip_regions, global_matrix_vector,
                      link_child_to_parent_transform_map);

  // Unconnected_views = all non-anonymous views (those with ViewRefs) not in the ViewTree.
  std::unordered_set<zx_koid_t> unconnected_views;
  for (const auto& [_, view_ref] : data.view_refs) {
    if (view_ref != nullptr) {
      const zx_koid_t koid = utils::ExtractKoid(*view_ref);
      if (view_tree.count(koid) == 0) {
        unconnected_views.emplace(koid);
      }
    }
  }

  // Copy all non-anonymous ViewRefs from the ViewRefMap.
  ViewRefMap named_view_refs;
  named_view_refs.reserve(data.view_refs.size());
  std::copy_if(data.view_refs.begin(), data.view_refs.end(),
               std::inserter(named_view_refs, named_view_refs.end()),
               [&anonymous = implicitly_anonymous_views](const auto& kv) {
                 return anonymous.count(kv.first) == 0;
               });

  // Note: The ViewTree represents a snapshot of the scene at a specific time. Because of this it's
  // important that it contains no references to live data. This means the hit testing closure must
  // contain only plain values or data with value semantics like shared_ptr<const>, to ensure that
  // it's safe to call from any thread.
  const auto hit_tester =
      [hit_test_data = HitTestingData{
           .transforms = data.topology_vector,
           .parent_indices = data.parent_indices,
           .root_transforms = data.root_transforms,
           .view_refs = std::move(named_view_refs),
           .hit_regions = data.hit_regions,
           .global_clip_regions = global_clip_regions,
       }](zx_koid_t start_node, glm::vec2 world_point, bool is_semantic_hit_test) {
        return HitTest(hit_test_data, start_node, world_point, is_semantic_hit_test);
      };

  return view_tree::SubtreeSnapshot{.root = root_koid,
                                    .view_tree = std::move(view_tree),
                                    .unconnected_views = std::move(unconnected_views),
                                    .hit_tester = std::move(hit_tester),
                                    // We do not currently support other compositors as subtrees.
                                    .tree_boundaries = {}};
}

}  // namespace flatland
