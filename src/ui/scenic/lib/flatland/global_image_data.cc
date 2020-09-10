// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_image_data.h"

#include <lib/syslog/cpp/macros.h>

namespace flatland {

// static
GlobalImageData ComputeGlobalImageData(const GlobalTopologyData::TopologyVector& global_topology,
                                       const UberStruct::InstanceMap& uber_structs) {
  GlobalIndexVector indices;
  GlobalImageVector images;

  for (uint32_t index = 0; index < global_topology.size(); index++) {
    // Every entry in the global topology comes from an UberStruct.
    const auto& handle = global_topology[index];
    const auto uber_struct_kv = uber_structs.find(handle.GetInstanceId());
    FX_DCHECK(uber_struct_kv != uber_structs.end());

    const auto image_kv = uber_struct_kv->second->images.find(handle);
    if (image_kv != uber_struct_kv->second->images.end()) {
      images.push_back(image_kv->second);
      indices.push_back(index);
    }
  }
  return {.indices = std::move(indices), .images = std::move(images)};
}

}  // namespace flatland
