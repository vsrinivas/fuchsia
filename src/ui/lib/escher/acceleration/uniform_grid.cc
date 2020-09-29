// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/acceleration/uniform_grid.h"

#include "src/ui/lib/escher/geometry/intersection.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/vk/buffer.h"

namespace escher {

namespace {

inline float DistanceToPlane(float D, float O, float pos) {
  return D != 0.f ? (pos - O) / D : 100000000.f;
}

inline bool GridCellIsValid(glm::ivec3 coordinates, int32_t resolution) {
  if (coordinates.x < 0 || coordinates.x >= resolution)
    return false;
  if (coordinates.y < 0 || coordinates.y >= resolution)
    return false;
  if (coordinates.z < 0 || coordinates.z >= resolution)
    return false;
  return true;
}

inline BoundingBox GetTriangleBoundingBox(glm::vec3 v1, glm::vec3 v2, glm::vec3 v3) {
  float min_x = glm::min(glm::min(v1.x, v2.x), v3.x);
  float min_y = glm::min(glm::min(v1.y, v2.y), v3.y);
  float min_z = glm::min(glm::min(v1.z, v2.z), v3.z);

  float max_x = glm::max(glm::max(v1.x, v2.x), v3.x);
  float max_y = glm::max(glm::max(v1.y, v2.y), v3.y);
  float max_z = glm::max(glm::max(v1.z, v2.z), v3.z);

  return BoundingBox(glm::vec3(min_x, min_y, min_z), glm::vec3(max_x, max_y, max_z));
}

}  // anonymous namespace

void UniformGrid::Construct(const std::vector<glm::vec3>& positions,
                            const std::vector<uint32_t>& indices, const BoundingBox& bounding_box) {
  uint32_t num_indices = static_cast<uint32_t>(indices.size());
  uint32_t num_vertices = static_cast<uint32_t>(positions.size());
  uint32_t num_triangles = num_indices / 3;

  // Set resolution to (roughly) the cube root of (roughly) num_triangles, so that the
  // number of cells is proportional to the number of triangles. Using the cubed root of
  // the total number of triangles has been shown to be a good guideline for uniform grid
  // construction by various researchers:
  // https://pharr.org/matt/blog/images/pbr-2001.pdf
  resolution_ = static_cast<uint32_t>(glm::max(cbrtf(static_cast<float>(num_triangles)), 1.f));
  FX_DCHECK(resolution_ > 0);

  // Save the vertex and bounding box information..
  vertices_ = std::vector<glm::vec3>(positions);
  bounds_ = bounding_box;

  // Calculate the extent for cells.
  glm::vec3 cell_extent = bounds_.extent() / static_cast<float>(resolution_) + glm::vec3(kEpsilon);
  FX_DCHECK(glm::all(glm::greaterThan(cell_extent, glm::vec3(kEpsilon))));

  // Assign all of the triangles to cells that they overlap.
  for (uint32_t i = 0; i < num_indices; i += 3) {
    uint32_t index_1 = indices[i];
    uint32_t index_2 = indices[i + 1];
    uint32_t index_3 = indices[i + 2];

    glm::vec3 v1 = vertices_[index_1];
    glm::vec3 v2 = vertices_[index_2];
    glm::vec3 v3 = vertices_[index_3];

    BoundingBox triangle_bbox = GetTriangleBoundingBox(v1, v2, v3);
    FX_DCHECK(!triangle_bbox.is_empty());

    glm::ivec3 cell_min = glm::floor(triangle_bbox.min() - bounds_.min()) / cell_extent;
    glm::ivec3 cell_max = glm::floor(triangle_bbox.max() - bounds_.min()) / cell_extent;
    for (int32_t x = cell_min.x; x <= cell_max.x; x++) {
      for (int32_t y = cell_min.y; y <= cell_max.y; y++) {
        for (int32_t z = cell_min.z; z <= cell_max.z; z++) {
          // Lazily create each cell once we find a triangle that intersects it.
          glm::ivec3 key(x, y, z);
          if (!cell_hash_.count(key)) {
            vec3 min = bounds_.min() + glm::vec3(key) * cell_extent;
            vec3 max = min + cell_extent;
            cell_hash_[key].set_bounds(BoundingBox(min, max));
          }

          cell_hash_[glm::ivec3(x, y, z)].AddTriangle(index_1, index_2, index_3);
        }
      }
    }
  }
}

bool UniformGrid::Intersect(const ray4& ray, float* out_distance) const {
  FX_DCHECK(out_distance);

  // Get ray properties.
  const glm::vec4 O = ray.origin;
  const glm::vec4 D = ray.direction;

  // Check that the ray intersects the bounding box first.
  Interval interval;
  if (!IntersectRayBox(ray, bounds_, &interval)) {
    return false;
  }
  glm::vec4 hit = ray.At(interval.min());
  glm::vec3 cell_extent = bounds_.extent() / static_cast<float>(resolution_) + glm::vec3(kEpsilon);

  // Compute the coordinates of the current cell
  glm::vec3 min = bounds_.min();
  glm::ivec3 cell_coordinates = (vec3(hit) - min) / cell_extent;
  FX_DCHECK(glm::all(glm::greaterThanEqual(cell_coordinates, glm::ivec3(0))));

  float px = glm::sign(D.x);
  float py = glm::sign(D.y);
  float pz = glm::sign(D.z);
  glm::ivec3 signs = glm::sign(D);
  glm::vec3 delta = glm::vec3(signs) * cell_extent / vec3(D + vec4(kEpsilon));

  float x_offset = (px > 0.f) ? px : 0.f;
  float y_offset = (py > 0.f) ? py : 0.f;
  float z_offset = (pz > 0.f) ? pz : 0.f;

  float next_x = DistanceToPlane(
      D.x, hit.x, min.x + (static_cast<float>(cell_coordinates.x) + x_offset) * cell_extent.x);
  float next_y = DistanceToPlane(
      D.y, hit.y, min.y + (static_cast<float>(cell_coordinates.y) + y_offset) * cell_extent.y);
  float next_z = DistanceToPlane(
      D.z, hit.z, min.z + (static_cast<float>(cell_coordinates.z) + z_offset) * cell_extent.z);
  FX_DCHECK(next_x >= 0.f) << next_x;
  FX_DCHECK(next_y >= 0.f) << next_y;
  FX_DCHECK(next_z >= 0.f) << next_z;

  // If we go beyond the extent of the uniform grid without finding a hit,
  // then stop looping.
  while (GridCellIsValid(cell_coordinates, resolution_)) {
    // The cell map is sparse, and so cells with no triangles that overlap
    // with them are not created.
    if (cell_hash_.count(cell_coordinates)) {
      const Cell& cell = cell_hash_.at(cell_coordinates);

      // Intersect the ray with the current cell, return true if there's a hit,
      // otherwise continue on to the next cell.
      if (cell.Intersect(ray, vertices_.data(), out_distance)) {
        return true;
      }
    }

    // Find the next cell to check.
    if (next_x < next_y && next_x < next_z) {
      cell_coordinates.x += signs.x;
      next_x += delta.x;
    } else if (next_y < next_x && next_y < next_z) {
      cell_coordinates.y += signs.y;
      next_y += delta.y;
    } else {
      cell_coordinates.z += signs.z;
      next_z += delta.z;
    }

    FX_DCHECK(!std::isnan(next_x) && !std::isinf(next_x));
    FX_DCHECK(!std::isnan(next_y) && !std::isinf(next_y));
    FX_DCHECK(!std::isnan(next_z) && !std::isinf(next_z));
  }

  // No hit.
  return false;
}

// Iterates over all of the triangles that overlap the cell and performs intersection
// tests on each of them. Returns true if any of the triangles are intersected. The
// out distance will be the distance to the closest triangle hit. Note that we cannot
// simply return early as soon as any triangle hit is found, because it might not be
// the closest triangle.
bool UniformGrid::Cell::Intersect(const ray4& ray, const vec3* vertices,
                                  float* out_distance) const {
  FX_DCHECK(vertices);
  FX_DCHECK(out_distance);
  FX_DCHECK(indices_.size() % 3 == 0);
  FX_DCHECK(!bounds_.is_empty());

  *out_distance = FLT_MAX;
  for (uint32_t i = 0; i < indices_.size(); i += 3) {
    const vec3& v0 = vertices[indices_[i]];
    const vec3& v1 = vertices[indices_[i + 1]];
    const vec3& v2 = vertices[indices_[i + 2]];

    float distance;
    if (IntersectRayTriangle(ray, v0, v1, v2, &distance)) {
      // Intersection only counts if it occcurs _within_ the current cell. Since a triangle
      // can overlap multiple cells, it's necessary to do this check before considering it
      // officially hit.
      if (bounds_.Contains(ray.At(distance))) {
        if (distance < *out_distance) {
          *out_distance = distance;
        }
      }
    }
  }

  // If there is a hit, the out distance will be less than FLT_MAX.
  return *out_distance < FLT_MAX;
}

}  // namespace escher
