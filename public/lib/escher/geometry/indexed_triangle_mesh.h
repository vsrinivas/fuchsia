// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_GEOMETRY_INDEXED_TRIANGLE_MESH_H_
#define LIB_ESCHER_GEOMETRY_INDEXED_TRIANGLE_MESH_H_

#include <vector>

#include "lib/escher/geometry/types.h"
#include "lib/escher/shape/mesh_spec.h"

namespace escher {

// Simple representation of an indexed triangle mesh, used during geometric
// algorithms before uploading the mesh to the GPU.  By separating positions
// from other attributes, it makes it easy to perform geometric operations such
// as splitting an edge where it intersects a plane then using the same
// interpolation parameter used to generate the new position to also interpolate
// the other attribute values.
template <typename PositionT, typename AttributeT>
struct IndexedTriangleMesh {
  using PositionType = PositionT;
  using AttributeType = AttributeT;
  using IndexType = MeshSpec::IndexType;
  using EdgeType = std::pair<IndexType, IndexType>;

  std::vector<IndexType> indices;
  std::vector<PositionType> positions;
  std::vector<AttributeType> attributes;

  void clear() {
    indices.clear();
    positions.clear();
    attributes.clear();
  }

  size_t index_count() const { return indices.size(); }
  size_t vertex_count() const { return positions.size(); }
  size_t triangle_count() const { return index_count() / 3; }

  // Return the total number of bytes used by vertex indices.
  size_t total_index_bytes() const {
    return index_count() * sizeof(MeshSpec::IndexType);
  }

  // Return the total number of bytes used by vertex position data.
  size_t total_position_bytes() const {
    return vertex_count() * sizeof(PositionT);
  }

  // Return the total number of bytes used by non-position vertex attributes.
  size_t total_attribute_bytes() const {
    return vertex_count() * sizeof(AttributeT);
  }

  // Return the total number of bytes used by indices, positions, and other
  // attributes.
  size_t total_bytes() const {
    return total_index_bytes() + total_position_bytes() +
           total_attribute_bytes();
  }

  // Return true if meshes are identical.  Will return false in all other cases,
  // including e.g. when the meshes are the same but all triangle indices are
  // rotated clockwise.
  bool operator==(
      const IndexedTriangleMesh<PositionT, AttributeT>& other) const {
    return indices == other.indices && positions == other.positions &&
           attributes == other.attributes;
  }
};

template <typename AttributeT>
using IndexedTriangleMesh2d = IndexedTriangleMesh<vec2, AttributeT>;

template <typename AttributeT>
using IndexedTriangleMesh3d = IndexedTriangleMesh<vec3, AttributeT>;

// Print IndexedTriangleMesh on ostream.
template <typename PositionT, typename AttributeT>
std::ostream& operator<<(
    std::ostream& str, const IndexedTriangleMesh<PositionT, AttributeT>& mesh) {
  str << "IndexedTriangleMesh[indices: " << mesh.index_count()
      << " vertices:" << mesh.vertex_count() << "\n";
  for (size_t tri = 0; tri + 2 < mesh.index_count(); tri += 3) {
    uint32_t ind0 = mesh.indices[tri];
    uint32_t ind1 = mesh.indices[tri + 1];
    uint32_t ind2 = mesh.indices[tri + 2];
    str << "tri " << tri / 3 << ": " << ind0 << "," << ind1 << "," << ind2
        << "    " << mesh.positions[ind0] << "," << mesh.positions[ind1] << ","
        << mesh.positions[ind2] << "\n";
  }
  for (size_t i = 0; i < mesh.vertex_count(); ++i) {
    str << "vert " << i << ": " << mesh.positions[i] << "     "
        << mesh.attributes[i] << "\n";
  }
  return str << "]";
}

}  // namespace escher

#endif  // LIB_ESCHER_GEOMETRY_INDEXED_TRIANGLE_MESH_H_
