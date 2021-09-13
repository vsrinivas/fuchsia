// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_matrix_data.h"

#include <lib/syslog/cpp/macros.h>

#include <cmath>

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_access.hpp>

namespace flatland {

const ImageSampleRegion kInvalidSampleRegion = {-1.f, -1.f, -1.f - 1.f};
const TransformClipRegion kUnclippedRegion = {-INT_MAX / 2, -INT_MAX / 2, INT_MAX, INT_MAX};

namespace {

bool Overlap(const TransformClipRegion& clip, const glm::vec2& origin, const glm::vec2& extent) {
  // DCHECK overflows.
  FX_DCHECK(clip.x + clip.width > 0);
  FX_DCHECK(clip.y + clip.height > 0);

  if (clip.x == kUnclippedRegion.x && clip.y == kUnclippedRegion.y &&
      clip.width == kUnclippedRegion.width && clip.height == kUnclippedRegion.height)
    return true;
  if (origin.x > clip.x + clip.width)
    return false;
  if (origin.x + extent.x < clip.x)
    return false;
  if (origin.y > clip.y + clip.height)
    return false;
  if (origin.y + extent.y < clip.y)
    return false;
  return true;
}

std::pair<glm::vec2, glm::vec2> ClipRectangle(const TransformClipRegion& clip,
                                              const glm::vec2& origin, const glm::vec2& extent) {
  if (!Overlap(clip, origin, extent)) {
    return {glm::vec2(0), glm::vec2(0)};
  }

  glm::vec2 result_origin, result_extent;
  result_origin.x = std::max(float(clip.x), origin.x);
  result_extent.x = std::min(float(clip.x + clip.width), origin.x + extent.x) - result_origin.x;

  result_origin.y = std::max(float(clip.y), origin.y);
  result_extent.y = std::min(float(clip.y + clip.height), origin.y + extent.y) - result_origin.y;

  return {result_origin, result_extent};
}

// TODO(fxbug.dev/77993): This will not produce the correct results for the display
// controller rendering pathway if a rotation is applied to the rectangle.
// Please see comment with same bug number in display_compositor.cc for more details.
escher::Rectangle2D CreateRectangle2D(const glm::mat3& matrix, const TransformClipRegion& clip,
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

  // Grab the origin and extent of the rectangle.
  auto origin = reordered_verts[0];
  auto extent = reordered_verts[1] - reordered_verts[3];

  // Now clip the origin and extent based on the clip rectangle.
  auto [clipped_origin, clipped_extent] = ClipRectangle(clip, origin, extent);

  // If no clipping happened, we can leave the UVs as is and return.
  if (origin == clipped_origin && extent == clipped_extent) {
    return escher::Rectangle2D(clipped_origin, clipped_extent, reordered_uvs);
  } else if (clipped_origin == glm::vec2(0) && clipped_extent == glm::vec2(0)) {
    return escher::Rectangle2D(clipped_origin, clipped_extent,
                               {glm::vec2(0), glm::vec2(0), glm::vec2(0), glm::vec2(0)});
  }

  // The rectangle was clipped, so we also have to clip the UV coordinates.
  auto lerp = [](float a, float b, float t) -> float { return a + t * (b - a); };
  float x_lerp = (clipped_origin.x - origin.x) / extent.x;
  float y_lerp = (clipped_origin.y - origin.y) / extent.y;
  float w_lerp = (clipped_origin.x + clipped_extent.x - origin.x) / extent.x;
  float h_lerp = (clipped_origin.y + clipped_extent.y - origin.y) / extent.y;
  glm::vec2 uv_0, uv_1, uv_2, uv_3;

  // Top Left
  uv_0.x = lerp(reordered_uvs[0].x, reordered_uvs[1].x, x_lerp);
  uv_0.y = lerp(reordered_uvs[0].y, reordered_uvs[3].y, y_lerp);

  // Top Right
  uv_1.x = lerp(reordered_uvs[0].x, reordered_uvs[1].x, w_lerp);
  uv_1.y = lerp(reordered_uvs[1].y, reordered_uvs[2].y, y_lerp);

  // Bottom Right
  uv_2.x = lerp(reordered_uvs[3].x, reordered_uvs[2].x, w_lerp);
  uv_2.y = lerp(reordered_uvs[1].y, reordered_uvs[2].y, h_lerp);

  // Bottom Left
  uv_3.x = lerp(reordered_uvs[3].x, reordered_uvs[2].x, x_lerp);
  uv_3.y = lerp(reordered_uvs[0].y, reordered_uvs[3].y, h_lerp);

  // This construction will CHECK if the extent is negative.
  return escher::Rectangle2D(clipped_origin, clipped_extent, {uv_0, uv_1, uv_2, uv_3});
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

GlobalImageSampleRegionVector ComputeGlobalImageSampleRegions(
    const GlobalTopologyData::TopologyVector& global_topology,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const UberStruct::InstanceMap& uber_structs) {
  GlobalImageSampleRegionVector sample_regions;
  if (global_topology.empty()) {
    return sample_regions;
  }

  sample_regions.reserve(global_topology.size());

  // The root entry's parent pointer points to itself, so special case it.
  const auto& root_handle = global_topology.front();
  const auto root_uber_struct_kv = uber_structs.find(root_handle.GetInstanceId());
  FX_DCHECK(root_uber_struct_kv != uber_structs.end());

  const auto root_regions_kv =
      root_uber_struct_kv->second->local_image_sample_regions.find(root_handle);

  if (root_regions_kv == root_uber_struct_kv->second->local_image_sample_regions.end()) {
    // Only non-image nodes should get here. This gets pruned out when we select for
    // content images.
    sample_regions.emplace_back(kInvalidSampleRegion);
  } else {
    sample_regions.emplace_back(root_regions_kv->second);
  }

  for (size_t i = 1; i < global_topology.size(); ++i) {
    const TransformHandle& handle = global_topology[i];
    const size_t parent_index = parent_indices[i];

    // Every entry in the global topology comes from an UberStruct.
    const auto uber_stuct_kv = uber_structs.find(handle.GetInstanceId());
    FX_DCHECK(uber_stuct_kv != uber_structs.end());

    const auto regions_kv = uber_stuct_kv->second->local_image_sample_regions.find(handle);

    if (regions_kv == uber_stuct_kv->second->local_image_sample_regions.end()) {
      // Only non-image nodes should get here. This gets pruned out when we select for
      // content images.
      sample_regions.emplace_back(kInvalidSampleRegion);
    } else {
      sample_regions.emplace_back(regions_kv->second);
    }
  }

  return sample_regions;
}

GlobalTransformClipRegionVector ComputeGlobalTransformClipRegions(
    const GlobalTopologyData::TopologyVector& global_topology,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const UberStruct::InstanceMap& uber_structs) {
  GlobalTransformClipRegionVector clip_regions;
  if (global_topology.empty()) {
    return clip_regions;
  }

  clip_regions.reserve(global_topology.size());

  // The root entry's parent pointer points to itself, so special case it.
  const auto& root_handle = global_topology.front();
  const auto root_uber_struct_kv = uber_structs.find(root_handle.GetInstanceId());
  FX_DCHECK(root_uber_struct_kv != uber_structs.end());

  const auto root_regions_kv = root_uber_struct_kv->second->local_clip_regions.find(root_handle);

  if (root_regions_kv == root_uber_struct_kv->second->local_clip_regions.end()) {
    clip_regions.emplace_back(kUnclippedRegion);
  } else {
    clip_regions.emplace_back(root_regions_kv->second);
  }

  for (size_t i = 1; i < global_topology.size(); ++i) {
    const TransformHandle& handle = global_topology[i];
    const size_t parent_index = parent_indices[i];

    // Every entry in the global topology comes from an UberStruct.
    const auto uber_stuct_kv = uber_structs.find(handle.GetInstanceId());
    FX_DCHECK(uber_stuct_kv != uber_structs.end());

    const auto regions_kv = uber_stuct_kv->second->local_clip_regions.find(handle);

    if (regions_kv == uber_stuct_kv->second->local_clip_regions.end()) {
      // Only non-image nodes should get here. This gets pruned out when we select for
      // content images.
      clip_regions.emplace_back(kUnclippedRegion);
    } else {
      // A clip region is bounded to that of its parent region.
      auto parent_clip = clip_regions[parent_index];
      auto curr_clip = regions_kv->second;

      glm::vec2 curr_origin = {curr_clip.x, curr_clip.y};
      glm::vec2 curr_extent = {curr_clip.width, curr_clip.height};

      auto [clipped_origin, clipped_extent] = ClipRectangle(parent_clip, curr_origin, curr_extent);
      clip_regions.emplace_back(TransformClipRegion{
          static_cast<int>(clipped_origin.x), static_cast<int>(clipped_origin.y),
          static_cast<int>(clipped_extent.x), static_cast<int>(clipped_extent.y)});
    }
  }

  return clip_regions;
}

// static
GlobalRectangleVector ComputeGlobalRectangles(
    const GlobalMatrixVector& matrices, const GlobalImageSampleRegionVector& sample_regions,
    const GlobalTransformClipRegionVector& clip_regions,
    const std::vector<allocation::ImageMetadata>& images) {
  GlobalRectangleVector rectangles;

  if (matrices.empty() || sample_regions.empty()) {
    return rectangles;
  }

  FX_DCHECK(matrices.size() == sample_regions.size());
  FX_DCHECK(matrices.size() == images.size());

  rectangles.reserve(matrices.size());

  const uint32_t num = matrices.size();
  for (uint32_t i = 0; i < num; i++) {
    const auto& s = sample_regions[i];
    const auto& image = images[i];
    auto w = image.width;
    auto h = image.height;
    FX_DCHECK(w >= 0.f && h >= 0.f);
    const std::array<glm::vec2, 4> uvs = {glm::vec2(s.x / w, s.y / h),
                                          glm::vec2((s.x + s.width) / w, s.y / h),
                                          glm::vec2((s.x + s.width) / w, (s.y + s.height) / h),
                                          glm::vec2(s.x / w, (s.y + s.height) / h)};

    rectangles.emplace_back(CreateRectangle2D(matrices[i], clip_regions[i], uvs));
  }

  return rectangles;
}

}  // namespace flatland
