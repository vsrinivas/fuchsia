// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_MATRIX_DATA_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_MATRIX_DATA_H_

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct.h"

namespace flatland {

// The list of global matrices for a particular global topology. Each entry is the global matrix
// (i.e. relative to the root TransformHandle) of the transform in the corresponding position of
// the |topology_vector| supplied to ComputeGlobalMatrices().
using GlobalMatrixVector = std::vector<glm::mat3>;

// The list of global image sample regions for a particular global topology.
using GlobalImageSampleRegionVector = std::vector<ImageSampleRegion>;

// The list of global transform clip regions for a particular global topology.
using GlobalTransformClipRegionVector = std::vector<TransformClipRegion>;

const extern ImageSampleRegion kInvalidSampleRegion;
const extern TransformClipRegion kUnclippedRegion;

// Computes the global transform matrix for each transform in |global_topology| using the local
// matrices in the |uber_structs|. If a transform doesn't have a local matrix present in the
// appropriate UberStruct, this function assumes that transform's local matrix is the identity
// matrix.
// TODO(fxbug.dev/77993): Remove matrices from flatland and make this a translation + size.
GlobalMatrixVector ComputeGlobalMatrices(
    const GlobalTopologyData::TopologyVector& global_topology,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const UberStruct::InstanceMap& uber_structs);

// Gathers the image sample regions for each transform in |global_topology| using the local
// image sample regions in the |uber_structs|. If a transform doesn't have image sample
// regions present in the appropriate UberStruct, this function assumes the region is null.
GlobalImageSampleRegionVector ComputeGlobalImageSampleRegions(
    const GlobalTopologyData::TopologyVector& global_topology,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const UberStruct::InstanceMap& uber_structs);

// Gathers the image sample regions for each transform in |global_topology| using the local
// image sample regions in the |uber_structs|. If a transform doesn't have image sample
// regions present in the appropriate UberStruct, this function assumes the region is null.
GlobalTransformClipRegionVector ComputeGlobalTransformClipRegions(
    const GlobalTopologyData::TopologyVector& global_topology,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const UberStruct::InstanceMap& uber_structs);

// The list of global rectangles for a particular global topology. Each entry is the global
// rectangle (i.e. relative to the root TransformHandle) of the transform in the corresponding
// position of the |matrices| supplied to ComputeGlobalRectangles().
using GlobalRectangleVector = std::vector<escher::Rectangle2D>;

// Computes the global rectangle for each matrix in |matrices|.
GlobalRectangleVector ComputeGlobalRectangles(const GlobalMatrixVector& matrices,
                                              const GlobalImageSampleRegionVector& sample_regions,
                                              const GlobalTransformClipRegionVector& clip_regions,
                                              const std::vector<allocation::ImageMetadata>& images);

// Templatized function to retrieve a vector of attributes that correspond to the provided
// indices, from the original vector.
template <typename T>
T SelectAttribute(const T& vector, const GlobalIndexVector& indices) {
  T selection;
  for (auto index : indices) {
    selection.push_back(vector[index]);
  }
  return selection;
}

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_MATRIX_DATA_H_
