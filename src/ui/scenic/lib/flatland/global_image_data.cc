// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_image_data.h"

#include <lib/syslog/cpp/macros.h>

namespace flatland {

GlobalOpacityVector ComputeGlobalOpacityValues(
    const GlobalTopologyData::TopologyVector& global_topology,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const UberStruct::InstanceMap& uber_structs) {
  GlobalOpacityVector opacity_values;

  if (global_topology.empty()) {
    return opacity_values;
  }

  opacity_values.reserve(global_topology.size());

  // The root entry's parent pointer points to itself, so special case it.
  const auto& root_handle = global_topology.front();
  const auto root_uber_struct_kv = uber_structs.find(root_handle.GetInstanceId());
  FX_DCHECK(root_uber_struct_kv != uber_structs.end());

  const auto root_opacity_kv = root_uber_struct_kv->second->local_opacity_values.find(root_handle);
  if (root_opacity_kv == root_uber_struct_kv->second->local_opacity_values.end()) {
    opacity_values.emplace_back(1.f);
  } else {
    const auto& opacity = root_opacity_kv->second;
    opacity_values.emplace_back(opacity);
  }

  for (size_t i = 1; i < global_topology.size(); ++i) {
    const TransformHandle& handle = global_topology[i];
    const size_t parent_index = parent_indices[i];

    // Every entry in the global topology comes from an UberStruct.
    const auto uber_stuct_kv = uber_structs.find(handle.GetInstanceId());
    FX_DCHECK(uber_stuct_kv != uber_structs.end());

    const auto opacity_kv = uber_stuct_kv->second->local_opacity_values.find(handle);
    if (opacity_kv == uber_stuct_kv->second->local_opacity_values.end()) {
      opacity_values.emplace_back(opacity_values[parent_index]);
    } else {
      opacity_values.emplace_back(opacity_values[parent_index] * opacity_kv->second);
    }
  }

  return opacity_values;
}

// static
GlobalImageData ComputeGlobalImageData(const GlobalTopologyData::TopologyVector& global_topology,
                                       const GlobalTopologyData::ParentIndexVector& parent_indices,
                                       const UberStruct::InstanceMap& uber_structs) {
  GlobalIndexVector indices;
  GlobalImageVector images;

  auto opacity_values = ComputeGlobalOpacityValues(global_topology, parent_indices, uber_structs);

  for (uint32_t index = 0; index < global_topology.size(); index++) {
    // Every entry in the global topology comes from an UberStruct.
    const auto& handle = global_topology[index];
    const auto uber_struct_kv = uber_structs.find(handle.GetInstanceId());
    FX_DCHECK(uber_struct_kv != uber_structs.end());

    const auto image_kv = uber_struct_kv->second->images.find(handle);
    if (image_kv != uber_struct_kv->second->images.end()) {
      auto image = image_kv->second;
      image.multiply_color[3] = opacity_values[index];
      images.push_back(image);
      indices.push_back(index);
    }
  }
  return {.indices = std::move(indices), .images = std::move(images)};
}

}  // namespace flatland
