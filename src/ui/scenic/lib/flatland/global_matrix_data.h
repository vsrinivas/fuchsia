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

// Computes the global transform matrix for each transform in |global_topology| using the local
// matrices in the |uber_structs|. If a transform doesn't have a local matrix present in the
// appropriate UberStruct, this function assumes that transform's local matrix is the identity
// matrix.
GlobalMatrixVector ComputeGlobalMatrices(
    const GlobalTopologyData::TopologyVector& global_topology,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const UberStruct::InstanceMap& uber_structs);

// The list of global rectangles for a particular global topology. Each entry is the global
// rectangle (i.e. relative to the root TransformHandle) of the transform in the corresponding
// position of the |matrices| supplied to ComputeGlobalRectangles().
using GlobalRectangleVector = std::vector<escher::Rectangle2D>;

// Computes the global rectangle for each matrix in |matrices|.
GlobalRectangleVector ComputeGlobalRectangles(const GlobalMatrixVector& matrices);

// Returns the vector of matrices that correspond to the provided indices, from the original
// global matrix vector.
GlobalMatrixVector SelectMatrices(const GlobalMatrixVector& matrices,
                                  const GlobalIndexVector& indices);

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_GLOBAL_MATRIX_DATA_H_
