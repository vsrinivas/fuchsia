// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_matrix_data.h"

#include <lib/syslog/cpp/macros.h>

#include <cmath>
#include <iterator>

#include "src/ui/lib/escher/geometry/types.h"

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_access.hpp>

namespace flatland {

using fuchsia::ui::composition::Orientation;

const ImageSampleRegion kInvalidSampleRegion = {-1.f, -1.f, -1.f - 1.f};
const TransformClipRegion kUnclippedRegion = {-INT_MAX / 2, -INT_MAX / 2, INT_MAX, INT_MAX};

namespace {

bool Overlap(const TransformClipRegion& clip, const glm::vec2& origin, const glm::vec2& extent) {
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

std::array<glm::vec3, 4> ConvertRectToVerts(fuchsia::math::Rect rect) {
  return {glm::vec3(static_cast<float>(rect.x), static_cast<float>(rect.y), 1),
          glm::vec3(static_cast<float>(rect.x + rect.width), static_cast<float>(rect.y), 1),
          glm::vec3(static_cast<float>(rect.x + rect.width),
                    static_cast<float>(rect.y + rect.height), 1),
          glm::vec3(static_cast<float>(rect.x), static_cast<float>(rect.y + rect.height), 1)};
}

std::array<glm::vec3, 4> ConvertRectFToVerts(fuchsia::math::RectF rect) {
  return {glm::vec3(rect.x, rect.y, 1), glm::vec3(rect.x + rect.width, rect.y, 1),
          glm::vec3(rect.x + rect.width, rect.y + rect.height, 1),
          glm::vec3(rect.x, rect.y + rect.height, 1)};
}

// Template to handle both vec2 and vec3 inputs.
template <typename T>
fuchsia::math::Rect ConvertVertsToRect(const std::array<T, 4>& verts) {
  return {.x = static_cast<int32_t>(verts[0].x),
          .y = static_cast<int32_t>(verts[0].y),
          .width = static_cast<int32_t>(fabs(verts[1].x - verts[0].x)),
          .height = static_cast<int32_t>(fabs(verts[2].y - verts[1].y))};
}

fuchsia::math::RectF ConvertVertsToRectF(const std::array<glm::vec2, 4>& verts) {
  return {.x = verts[0].x,
          .y = verts[0].y,
          .width = fabs(verts[1].x - verts[0].x),
          .height = fabs(verts[2].y - verts[1].y)};
}

// Assume that the 4 vertices represent a rectangle, and are provided in clockwise order,
// starting at the top-left corner. Return a tuple of the transformed vertices as well as
// those same transformed vertices reordered so that they are in clockwise order starting
// at the top-left corner.
std::pair<std::array<glm::vec2, 4>, std::array<glm::vec2, 4>> MatrixMultiplyVerts(
    const glm::mat3& matrix, const std::array<glm::vec3, 4>& in_verts) {
  const std::array<glm::vec2, 4> verts = {
      matrix * in_verts[0],
      matrix * in_verts[1],
      matrix * in_verts[2],
      matrix * in_verts[3],
  };

  float min_x = FLT_MAX, min_y = FLT_MAX;
  float max_x = -FLT_MAX, max_y = -FLT_MAX;
  for (uint32_t i = 0; i < 4; i++) {
    min_x = std::min(min_x, verts[i].x);
    min_y = std::min(min_y, verts[i].y);
    max_x = std::max(max_x, verts[i].x);
    max_y = std::max(max_y, verts[i].y);
  }

  return {verts,
          {
              glm::vec2(min_x, min_y),  // top_left
              glm::vec2(max_x, min_y),  // top_right
              glm::vec2(max_x, max_y),  // bottom_right
              glm::vec2(min_x, max_y),  // bottom_left
          }};
}

fuchsia::math::Rect MatrixMultiplyRect(const glm::mat3& matrix, fuchsia::math::Rect rect) {
  return ConvertVertsToRect(std::get<1>(MatrixMultiplyVerts(matrix, ConvertRectToVerts(rect))));
}

fuchsia::math::RectF MatrixMultiplyRectF(const glm::mat3& matrix, fuchsia::math::RectF rect) {
  return ConvertVertsToRectF(std::get<1>(MatrixMultiplyVerts(matrix, ConvertRectFToVerts(rect))));
}

ImageRect CreateImageRect(const glm::mat3& matrix, const TransformClipRegion& clip,
                          const std::array<glm::ivec2, 4>& texel_uvs) {
  // The local space of the renderable has its top-left origin point at (0,0) and grows
  // downward and to the right, so that the bottom-right point is at (1,1). We apply
  // the matrix to the four points that represent this unit square to get the points in
  // the global coordinate space.
  //
  // Note that the verts provided are 2D homogenous coordinates, so the third value is always equal
  // to 1. These are NOT 3D vectors with x, y, z values.
  auto [verts, reordered_verts] = MatrixMultiplyVerts(matrix, {
                                                                  glm::vec3(0, 0, 1),
                                                                  glm::vec3(1, 0, 1),
                                                                  glm::vec3(1, 1, 1),
                                                                  glm::vec3(0, 1, 1),
                                                              });

  std::array<glm::vec2, 4> reordered_uvs;
  // Will equal the index of the vert located at the origin in the reordered verts.
  int vert_index = 0;
  bool vert_index_set = false;
  for (uint32_t i = 0; i < 4; i++) {
    if (glm::all(glm::epsilonEqual(reordered_verts[0], verts[i], 0.001f))) {
      vert_index = i;
      vert_index_set = true;
      break;
    }
  }

  FX_DCHECK(vert_index_set) << "Expected |vert_index| to be set";

  // Maps the calculated |vert_index| value to the global Orientation specified by the matrix. Note
  // this conversion only considers orientation and not reflections. Reflections are a property of
  // Image Content only, not Transforms (or Viewports), and so are not handled here.
  constexpr Orientation kIndexToOrientation[4] = {
      // If |vert_index| = 0, then the list is in the same order (no rotation).
      Orientation::CCW_0_DEGREES,
      // If |vert_index| = 1, then the verts have been rotated by 90 degrees (top-left is now
      // top-right).
      Orientation::CCW_90_DEGREES,
      // If |vert_index| = 2, then the verts have been rotated by 180 degrees (top-left is now
      // bottom-right).
      Orientation::CCW_180_DEGREES,
      // If |vert_index| = 3, then the verts have been rotated by 270 degrees (top-left is now
      // bottom-left).
      Orientation::CCW_270_DEGREES};

  const Orientation orientation = kIndexToOrientation[vert_index];

  // Grab the origin, extent and orientation of the rectangle.
  auto origin = reordered_verts[0];
  auto extent = reordered_verts[2] - reordered_verts[0];

  // Now clip the origin and extent based on the clip rectangle.
  auto [clipped_origin, clipped_extent] = ClipRectangle(clip, origin, extent);

  if (origin == clipped_origin && extent == clipped_extent) {
    // If no clipping happened, we can leave the UVs as is and return.
    return ImageRect(clipped_origin, clipped_extent, texel_uvs, orientation);
  } else if (clipped_origin == glm::vec2(0) && clipped_extent == glm::vec2(0)) {
    // The entire rectangle is outside of the clip region.
    return ImageRect(clipped_origin, clipped_extent,
                     {glm::vec2(0), glm::vec2(0), glm::vec2(0), glm::vec2(0)}, orientation);
  }

  // The rectangle was clipped, so we also have to clip the UV coordinates.
  auto lerp = [](float a, float b, float t) -> float { return a + t * (b - a); };
  float x_lerp = (clipped_origin.x - origin.x) / extent.x;
  float y_lerp = (clipped_origin.y - origin.y) / extent.y;
  float w_lerp = (clipped_origin.x + clipped_extent.x - origin.x) / extent.x;
  float h_lerp = (clipped_origin.y + clipped_extent.y - origin.y) / extent.y;

  // The clipped region, the new origin and the new extent already account for orientation. However,
  // this is not the case for the texel UVs. If the rectangle was rotated by 90 or 270, then the
  // x-axis in "texture-space" will now be clipped by the "vertical-axis" of the clip rectangle.
  // The following calculations need to account for this.
  //
  // Once the correct UV coordinates are calculated, they are returned in 'regular' order i.e. in
  // texture space, starting at the top-left corner and rotating clockwise.
  //
  // Note that uv.x is equivalent to uv[0] and uv.y is equivalent to uv[1].
  const auto rotated_u = vert_index % 2;
  const auto rotated_v = (vert_index + 1) % 2;

  std::array<glm::ivec2, 4> uvs;

  const uint32_t idx = vert_index;
  const uint32_t idx_1 = (vert_index + 1) % 4;
  const uint32_t idx_2 = (vert_index + 2) % 4;
  const uint32_t idx_3 = (vert_index + 3) % 4;

  // Top Left (of texture).
  uvs[idx][rotated_u] = lerp(texel_uvs[idx][rotated_u], texel_uvs[idx_1][rotated_u], x_lerp);
  uvs[idx][rotated_v] = lerp(texel_uvs[idx][rotated_v], texel_uvs[idx_3][rotated_v], y_lerp);

  // Top Right (of texture).
  uvs[idx_1][rotated_u] = lerp(texel_uvs[idx][rotated_u], texel_uvs[idx_1][rotated_u], w_lerp);
  uvs[idx_1][rotated_v] = lerp(texel_uvs[idx_1][rotated_v], texel_uvs[idx_2][rotated_v], y_lerp);

  // Bottom Right (of texture).
  uvs[idx_2][rotated_u] = lerp(texel_uvs[idx_3][rotated_u], texel_uvs[idx_2][rotated_u], w_lerp);
  uvs[idx_2][rotated_v] = lerp(texel_uvs[idx_1][rotated_v], texel_uvs[idx_2][rotated_v], h_lerp);

  // Bottom Left (of texture).
  uvs[idx_3][rotated_u] = lerp(texel_uvs[idx_3][rotated_u], texel_uvs[idx_2][rotated_u], x_lerp);
  uvs[idx_3][rotated_v] = lerp(texel_uvs[idx][rotated_v], texel_uvs[idx_3][rotated_v], h_lerp);

  // This construction will CHECK if the extent is negative.
  return ImageRect(clipped_origin, clipped_extent, std::move(uvs), orientation);
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
    const auto uber_struct_kv = uber_structs.find(handle.GetInstanceId());
    FX_DCHECK(uber_struct_kv != uber_structs.end());

    const auto matrix_kv = uber_struct_kv->second->local_matrices.find(handle);

    if (matrix_kv == uber_struct_kv->second->local_matrices.end()) {
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
  sample_regions.reserve(global_topology.size());
  for (size_t i = 0; i < global_topology.size(); ++i) {
    // Every entry in the global topology comes from an UberStruct.
    const TransformHandle& handle = global_topology[i];
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
    const GlobalMatrixVector& matrix_vector, const UberStruct::InstanceMap& uber_structs) {
  FX_DCHECK(global_topology.size() == parent_indices.size());
  FX_DCHECK(global_topology.size() == matrix_vector.size());

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

  // Process the root separately from the rest of the tree.
  if (root_regions_kv == root_uber_struct_kv->second->local_clip_regions.end()) {
    clip_regions.emplace_back(kUnclippedRegion);
  } else {
    clip_regions.emplace_back(MatrixMultiplyRect(matrix_vector[0], root_regions_kv->second));
  }

  for (size_t i = 1; i < global_topology.size(); ++i) {
    const TransformHandle& handle = global_topology[i];
    const size_t parent_index = parent_indices[i];
    auto parent_clip = clip_regions[parent_index];

    // Every entry in the global topology comes from an UberStruct.
    const auto uber_stuct_kv = uber_structs.find(handle.GetInstanceId());
    FX_DCHECK(uber_stuct_kv != uber_structs.end());
    const auto regions_kv = uber_stuct_kv->second->local_clip_regions.find(handle);

    // A clip region is bounded to that of its parent region. If the current clip region
    // is empty, then it defaults to that of its parent. Otherwise, we must find the
    // intersection of the parent clip region and the current clip region, in the global
    // coordinate space.
    if (regions_kv == uber_stuct_kv->second->local_clip_regions.end()) {
      clip_regions.emplace_back(parent_clip);
    } else {
      // Calculate the global position of the current clip region.
      auto curr_clip = MatrixMultiplyRect(matrix_vector[i], regions_kv->second);

      // Calculate the intersection of the current clip with its parent.
      glm::vec2 curr_origin = {curr_clip.x, curr_clip.y};
      glm::vec2 curr_extent = {curr_clip.width, curr_clip.height};
      auto [clipped_origin, clipped_extent] = ClipRectangle(parent_clip, curr_origin, curr_extent);

      // Add the intersection to the global clip vector.
      clip_regions.emplace_back(TransformClipRegion{
          static_cast<int>(clipped_origin.x), static_cast<int>(clipped_origin.y),
          static_cast<int>(clipped_extent.x), static_cast<int>(clipped_extent.y)});
    }
  }

  return clip_regions;
}

GlobalHitRegionsMap ComputeGlobalHitRegions(
    const GlobalTopologyData::TopologyVector& global_topology,
    const GlobalTopologyData::ParentIndexVector& parent_indices,
    const GlobalMatrixVector& matrix_vector, const UberStruct::InstanceMap& uber_structs) {
  FX_DCHECK(global_topology.size() == parent_indices.size());
  FX_DCHECK(global_topology.size() == matrix_vector.size());

  GlobalHitRegionsMap global_hit_regions;

  for (size_t i = 0; i < global_topology.size(); ++i) {
    const TransformHandle& handle = global_topology[i];

    // Every entry in the global topology comes from an UberStruct.
    const auto uber_struct_kv = uber_structs.find(handle.GetInstanceId());
    FX_DCHECK(uber_struct_kv != uber_structs.end());

    const auto regions_vec_kv = uber_struct_kv->second->local_hit_regions_map.find(handle);

    if (regions_vec_kv != uber_struct_kv->second->local_hit_regions_map.end()) {
      for (auto& local_hit_region : regions_vec_kv->second) {
        auto local_rect = local_hit_region.region;

        // Calculate the global position of the current hit region.
        auto global_rect = MatrixMultiplyRectF(matrix_vector[i], local_rect);

        global_hit_regions[handle].push_back({global_rect, local_hit_region.hit_test});
      }
    }
  }

  return global_hit_regions;
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
    const auto& matrix = matrices[i];
    const auto& clip = clip_regions[i];
    const auto& sample = sample_regions[i];
    const auto& image = images[i];

    {
      auto w = image.width;
      auto h = image.height;

      if (w > 0 && h > 0) {
        FX_DCHECK(sample.x >= 0 && (sample.x + sample.width) <= w);
        FX_DCHECK(sample.y >= 0 && (sample.y + sample.height) <= h);
      }
    }

    const std::array<glm::ivec2, 4> unclipped_texel_uvs = {
        glm::ivec2(sample.x, sample.y), glm::ivec2(sample.x + sample.width, sample.y),
        glm::ivec2(sample.x + sample.width, sample.y + sample.height),
        glm::ivec2(sample.x, sample.y + sample.height)};

    rectangles.emplace_back(CreateImageRect(matrix, clip, unclipped_texel_uvs));
  }

  return rectangles;
}

void CullRectangles(GlobalRectangleVector* rectangles_in_out, GlobalImageVector* images_in_out,
                    uint64_t display_width, uint64_t display_height) {
  FX_DCHECK(rectangles_in_out && images_in_out);
  FX_DCHECK(rectangles_in_out->size() == images_in_out->size());
  unsigned length = rectangles_in_out->size();

  auto is_occluder = [display_width, display_height](
                         const ImageRect& rectangle,
                         const allocation::ImageMetadata& image) -> bool {
    // Only cull if the rect is opaque.
    auto is_opaque = image.blend_mode == fuchsia::ui::composition::BlendMode::SRC;

    // If the rect is full screen (or larger), and opaque, clear the output vectors.
    return (is_opaque && rectangle.origin.x <= 0 && rectangle.origin.y <= 0 &&
            rectangle.extent.x >= display_width && rectangle.extent.y >= display_height);
  };

  // Find the index of the last occluder.
  unsigned i = 0, occluder_index = 0;
  for (; i < length; i++) {
    if (is_occluder((*rectangles_in_out)[i], (*images_in_out)[i])) {
      occluder_index = i;
    }
  }

  // Move all of the remaining renderable data into the output vectors. Entries get erased
  // if they occur before the last occluder index, or if the rectangle at that entry is empty.
  {
    auto is_rect_empty = [](const ImageRect& rect) {
      return rect.extent.x <= 0.f && rect.extent.y <= 0.f;
    };

    unsigned index = 0;
    images_in_out->erase(
        std::remove_if(images_in_out->begin(), images_in_out->end(),
                       [&index, &occluder_index, &is_rect_empty,
                        &rectangles_in_out](const allocation::ImageMetadata& image) {
                         auto curr_index = index++;
                         return curr_index < occluder_index ||
                                is_rect_empty((*rectangles_in_out)[curr_index]);
                       }),
        images_in_out->end());

    index = 0;
    rectangles_in_out->erase(
        std::remove_if(rectangles_in_out->begin(), rectangles_in_out->end(),
                       [&index, &occluder_index, &is_rect_empty](const ImageRect& rect) {
                         auto curr_index = index++;
                         return curr_index < occluder_index || is_rect_empty(rect);
                       }),
        rectangles_in_out->end());
  }
}

}  // namespace flatland
