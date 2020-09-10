// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_matrix_data.h"

#include <lib/syslog/cpp/macros.h>

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_access.hpp>

namespace flatland {

namespace {

constexpr std::array<glm::vec2, 4> kDefaultUvs = {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1),
                                                  glm::vec2(0, 1)};

escher::Rectangle2D CreateRectangle2D(const glm::mat3& matrix,
                                      const std::array<glm::vec2, 4>& uvs) {
  // The local-space of the renderable has its top-left origin point at (0,0) and grows
  // downward and to the right, so that the bottom-right point is at (1,-1). We apply
  // the matrix to the four points that represent this unit square to get the points
  // in the global coordinate space.
  const glm::vec2 verts[4] = {
      matrix * glm::vec3(0, 0, 1),
      matrix * glm::vec3(1, 0, 1),
      matrix * glm::vec3(1, -1, 1),
      matrix * glm::vec3(0, -1, 1),
  };

  float min_x = FLT_MAX, min_y = FLT_MAX;
  float max_x = -FLT_MAX, max_y = -FLT_MAX;
  for (uint32_t i = 0; i < 4; i++) {
    min_x = std::min(min_x, verts[i].x);
    min_y = std::min(min_y, verts[i].y);
    max_x = std::max(max_x, verts[i].x);
    max_y = std::max(max_y, verts[i].y);
  }

  glm::vec2 reordered_verts[4] = {
      glm::vec2(min_x, max_y),  // top_left
      glm::vec2(max_x, max_y),  // top_right
      glm::vec2(max_x, min_y),  // bottom_right
      glm::vec2(min_x, min_y),  // bottom_left
  };

  std::array<glm::vec2, 4> reordered_uvs;
  for (uint32_t i = 0; i < 4; i++) {
    for (uint32_t j = 0; j < 4; j++) {
      if (glm::all(glm::epsilonEqual(reordered_verts[i], verts[j], 0.001f))) {
        reordered_uvs[i] = uvs[j];
        break;
      }
    }
  }

  // This construction will CHECK if the extent is negative.
  escher::Rectangle2D rectangle(reordered_verts[0], reordered_verts[1] - reordered_verts[3],
                                reordered_uvs);
  return rectangle;
}

}  // namespace

// static
GlobalMatrixVector ComputeGlobalMatrices(
    const GlobalTopologyData::TopologyVector& global_topology,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const UberStruct::InstanceMap& uber_structs) {
  GlobalMatrixVector matrices;

  if (global_topology.empty()) {
    return matrices;
  }

  matrices.reserve(global_topology.size());

  // The root entry's parent pointer points to itself, so special case it.
  const auto& root_handle = global_topology.front();
  const auto root_uber_struct_kv = uber_structs.find(root_handle.GetInstanceId());
  FX_DCHECK(root_uber_struct_kv != uber_structs.end());

  const auto root_matrix_kv = root_uber_struct_kv->second->local_matrices.find(root_handle);
  if (root_matrix_kv == root_uber_struct_kv->second->local_matrices.end()) {
    matrices.emplace_back(glm::mat3());
  } else {
    const auto& matrix = root_matrix_kv->second;
    matrices.emplace_back(matrix);
  }

  for (size_t i = 1; i < global_topology.size(); ++i) {
    const TransformHandle& handle = global_topology[i];
    const size_t parent_index = parent_indices[i];

    // Every entry in the global topology comes from an UberStruct.
    const auto uber_stuct_kv = uber_structs.find(handle.GetInstanceId());
    FX_DCHECK(uber_stuct_kv != uber_structs.end());

    const auto matrix_kv = uber_stuct_kv->second->local_matrices.find(handle);
    if (matrix_kv == uber_stuct_kv->second->local_matrices.end()) {
      matrices.emplace_back(matrices[parent_index]);
    } else {
      matrices.emplace_back(matrices[parent_index] * matrix_kv->second);
    }
  }

  return matrices;
}

// static
GlobalMatrixVector SelectMatrices(const GlobalMatrixVector& matrices,
                                  const GlobalIndexVector& indices) {
  GlobalMatrixVector selection;

  for (auto index : indices) {
    selection.push_back(matrices[index]);
  }

  return selection;
}

// static
GlobalRectangleVector ComputeGlobalRectangles(const GlobalMatrixVector& matrices) {
  GlobalRectangleVector rectangles;

  if (matrices.empty()) {
    return rectangles;
  }

  rectangles.reserve(matrices.size());

  for (const auto& matrix : matrices) {
    rectangles.emplace_back(CreateRectangle2D(matrix, kDefaultUvs));
  }

  return rectangles;
}

}  // namespace flatland
