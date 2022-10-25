// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_MATRIX_DATA_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_MATRIX_DATA_H_

#include "src/ui/scenic/lib/flatland/flatland_types.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
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

// The set of per-transform hit regions for a particular global topology.
using GlobalHitRegionsMap =
    std::unordered_map<TransformHandle, std::vector<fuchsia::ui::composition::HitRegion>>;

const extern ImageSampleRegion kInvalidSampleRegion;
const extern TransformClipRegion kUnclippedRegion;

// Computes the global transform matrix for each transform in |global_topology| using the local
// matrices in the |uber_structs|. If a transform doesn't have a local matrix present in the
// appropriate UberStruct, this function assumes that transform's local matrix is the identity
// matrix.
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
// Since clip regions are specified in the local space of the transform they are attached to,
// this function transforms those into global clip regions before returning them. This requires
// the global matrix vector to be passed along as a parameter.
GlobalTransformClipRegionVector ComputeGlobalTransformClipRegions(
    const GlobalTopologyData::TopologyVector& global_topology,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const GlobalMatrixVector& matrix_vector, const UberStruct::InstanceMap& uber_structs);

// Aggregates the set of local hit regions for each transform in |global_topology| into a map of
// global hit regions. This process involves two steps: first, convert all hit regions which are
// in each transform's local space into world space, and then clip the hit regions to the
// transform's clip region.
GlobalHitRegionsMap ComputeGlobalHitRegions(
    const GlobalTopologyData::TopologyVector& global_topology,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const GlobalMatrixVector& matrix_vector, const UberStruct::InstanceMap& uber_structs);

// The list of global rectangles for a particular global topology. Each entry is the global
// rectangle (i.e. relative to the root TransformHandle) of the transform in the corresponding
// position of the |matrices| supplied to ComputeGlobalRectangles().
using GlobalRectangleVector = std::vector<ImageRect>;

// Computes the global rectangle for each matrix in |matrices|.
GlobalRectangleVector ComputeGlobalRectangles(const GlobalMatrixVector& matrices,
                                              const GlobalImageSampleRegionVector& sample_regions,
                                              const GlobalTransformClipRegionVector& clip_regions,
                                              const std::vector<allocation::ImageMetadata>& images);

// Simple culling algorithm that checks if any of the input rectangles cover the entire display,
// and if so, culls all rectangles that came before them (since rectangles are implicitly sorted
// according to depth, with the first entry being the furthest back, this has the effect of
// eliminating all rectangles behind the full-screen one). Also culls any rectangle that has
// no size (0,0).
void CullRectangles(GlobalRectangleVector* rectangles_in_out, GlobalImageVector* images_in_out,
                    uint64_t display_width, uint64_t display_height);

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
