// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_IMAGE_DATA_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_IMAGE_DATA_H_

#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/flatland/uber_struct.h"

namespace flatland {

// The list of Images for a particular global topology. Entries in the list are sorted in the order
// they should be rendered.
using GlobalImageVector = std::vector<ImageMetadata>;

// Struct combining a vector of sorted images and a vector of indices corresponding to the
// transforms each image is paired with. Both vectors should be the same length.
struct GlobalImageData {
  GlobalIndexVector indices;
  GlobalImageVector images;
};

// Computes the list of Images given a |global_topology| and the |uber_structs| used to generate
// that topology. Note that not all TransformHandles will have Images, so the return value will
// have fewer entries than there are in the global topology.
GlobalImageData ComputeGlobalImageData(const GlobalTopologyData::TopologyVector& global_topology,
                                       const UberStruct::InstanceMap& uber_structs);

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_IMAGE_DATA_H_
