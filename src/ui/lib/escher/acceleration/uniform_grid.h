// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_ACCELERATION_UNIFORM_GRID_H_
#define SRC_UI_LIB_ESCHER_ACCELERATION_UNIFORM_GRID_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/mesh/indexed_triangle_mesh.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/util/hash_map.h"

namespace escher {

// A uniform grid is a data structure meant for accelerating
// ray-mesh intersections.
class UniformGrid {
 public:
  template <typename AttrT1, typename AttrT2, typename AttrT3>
  static std::unique_ptr<UniformGrid> New(
      const IndexedTriangleMesh3d<AttrT1, AttrT2, AttrT3>& mesh) {
    // Force the client to make sure the mesh is valid and non-empty.
    if (!mesh.IsValid() || mesh.indices.size() == 0 || mesh.positions.size() == 0 ||
        mesh.bounding_box.is_empty()) {
      FXL_CHECK(false);
      return nullptr;
    }

    // Create uniform grid.
    std::unique_ptr<UniformGrid> uniform_grid = std::make_unique<UniformGrid>();
    uniform_grid->Construct(mesh.positions, mesh.indices, mesh.bounding_box);
    return uniform_grid;
  }

  UniformGrid() {}
  ~UniformGrid() {}

  bool Intersect(const ray4& ray, float* out_distance) const;

  uint32_t resolution() const { return resolution_; }

 private:
  class Cell {
   public:
    bool Intersect(const ray4& ray, const vec3* vertices, float* out_distance) const;

    // Adds a triangle that intersects the cell.
    void AddTriangle(uint32_t v0, uint32_t v1, uint32_t v2) {
      indices_.push_back(v0);
      indices_.push_back(v1);
      indices_.push_back(v2);
    }

    void set_bounds(BoundingBox bounds) { bounds_ = bounds; }

   private:
    std::vector<uint32_t> indices_;
    BoundingBox bounds_;
  };

  void Construct(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
                 const BoundingBox& bounding_box);

  HashMap<glm::ivec3, Cell> cell_hash_;
  BoundingBox bounds_;
  std::vector<vec3> vertices_;
  uint32_t resolution_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_ACCELERATION_UNIFORM_GRID_H_
