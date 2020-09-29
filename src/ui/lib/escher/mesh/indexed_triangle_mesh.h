// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_MESH_INDEXED_TRIANGLE_MESH_H_
#define SRC_UI_LIB_ESCHER_MESH_INDEXED_TRIANGLE_MESH_H_

#include <vector>

#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/shape/mesh_spec.h"

namespace escher {

// Simple representation of an indexed triangle mesh, used during geometric
// algorithms before uploading the mesh to the GPU.  By separating positions
// from other attributes, it makes it easy to perform geometric operations such
// as splitting an edge where it intersects a plane then using the same
// interpolation parameter used to generate the new position to also interpolate
// the other attribute values.
template <typename PositionT, typename AttrT1 = nullptr_t, typename AttrT2 = nullptr_t,
          typename AttrT3 = nullptr_t>
struct IndexedTriangleMesh {
  using PositionType = PositionT;
  using AttributeType1 = AttrT1;
  using AttributeType2 = AttrT2;
  using AttributeType3 = AttrT3;

  using IndexType = MeshSpec::IndexType;
  using EdgeType = std::pair<IndexType, IndexType>;

  BoundingBox bounding_box;
  std::vector<IndexType> indices;
  std::vector<PositionType> positions;
  std::vector<AttributeType1> attributes1;
  std::vector<AttributeType2> attributes2;
  std::vector<AttributeType3> attributes3;

  void clear() {
    indices.clear();
    positions.clear();
    attributes1.clear();
    attributes2.clear();
    attributes3.clear();
  }

  uint32_t index_count() const { return static_cast<uint32_t>(indices.size()); }
  uint32_t vertex_count() const { return static_cast<uint32_t>(positions.size()); }
  uint32_t triangle_count() const { return index_count() / 3; }

  void resize_indices(uint32_t num_indices) {
    FX_DCHECK(num_indices % 3 == 0);
    indices.resize(num_indices);
  }

  void resize_vertices(uint32_t num_vertices) {
    positions.resize(num_vertices);
    if (!std::is_same<AttrT1, nullptr_t>::value) {
      attributes1.resize(num_vertices);
    }
    if (!std::is_same<AttrT2, nullptr_t>::value) {
      attributes2.resize(num_vertices);
    }
    if (!std::is_same<AttrT3, nullptr_t>::value) {
      attributes3.resize(num_vertices);
    }
  }

  // Return the total number of bytes used by vertex indices.
  size_t total_index_bytes() const { return index_count() * sizeof(MeshSpec::IndexType); }

  size_t sizeof_attribute1() const {
    return std::is_same<AttrT1, nullptr_t>::value ? 0 : sizeof(AttrT1);
  };

  size_t sizeof_attribute2() const {
    return std::is_same<AttrT2, nullptr_t>::value ? 0 : sizeof(AttrT2);
  };

  size_t sizeof_attribute3() const {
    return std::is_same<AttrT3, nullptr_t>::value ? 0 : sizeof(AttrT3);
  };

  // Return the total number of bytes used by vertex position data.
  size_t total_position_bytes() const { return vertex_count() * sizeof(PositionT); }

  // Return the total number of bytes used by non-position vertex attributes.
  size_t total_attribute1_bytes() const { return vertex_count() * sizeof_attribute1(); }
  size_t total_attribute2_bytes() const { return vertex_count() * sizeof_attribute2(); }
  size_t total_attribute3_bytes() const { return vertex_count() * sizeof_attribute3(); }

  // Return the total number of bytes used by indices, positions, and other
  // attributes.
  size_t total_bytes() const {
    return total_index_bytes() + total_position_bytes() + total_attribute1_bytes() +
           total_attribute2_bytes() + total_attribute3_bytes();
  }

  // Return true if the mesh passes basic sanity checks, and false otherwise.
  bool IsValid() const {
    if (index_count() % 3 != 0) {
      FX_LOGS(ERROR) << "index-count must be a multiple of 3: " << index_count();
      return false;
    }
    for (auto i : indices) {
      if (i >= vertex_count()) {
        FX_LOGS(ERROR) << "index exceeds vertex-count: " << i << ", " << vertex_count();
        return false;
      }
    }
    if (std::is_same<AttrT1, nullptr_t>::value) {
      if (attributes1.size() != 0) {
        FX_LOGS(ERROR) << "count of null attribute1 must be zero: " << attributes1.size();
        return false;
      }
    } else if (attributes1.size() != vertex_count()) {
      FX_LOGS(ERROR) << "count of attribute1 must match vertex-count: " << attributes1.size()
                     << ", " << vertex_count();
      return false;
    }
    if (std::is_same<AttrT2, nullptr_t>::value) {
      if (attributes2.size() != 0) {
        FX_LOGS(ERROR) << "count of null attribute2 must be zero: " << attributes2.size();
        return false;
      }
    } else if (attributes2.size() != vertex_count()) {
      FX_LOGS(ERROR) << "count of attribute2 must match vertex-count: " << attributes2.size()
                     << ", " << vertex_count();
      return false;
    }
    if (std::is_same<AttrT3, nullptr_t>::value) {
      if (attributes3.size() != 0) {
        FX_LOGS(ERROR) << "count of null attribute3 must be zero: " << attributes3.size();
        return false;
      }
    } else if (attributes3.size() != vertex_count()) {
      FX_LOGS(ERROR) << "count of attribute3 must match vertex-count: " << attributes3.size()
                     << ", " << vertex_count();
      return false;
    }
    // Valid!
    return true;
  }

  // Return true if meshes are identical.  Will return false in all other cases,
  // including e.g. when the meshes are the same but all triangle indices are
  // rotated clockwise.
  bool operator==(const IndexedTriangleMesh<PositionT, AttrT1>& other) const {
    return indices == other.indices && positions == other.positions &&
           attributes1 == other.attributes1 && attributes2 == other.attributes2 &&
           attributes3 == other.attributes3;
  }
};

template <typename AttrT1 = nullptr_t, typename AttrT2 = nullptr_t, typename AttrT3 = nullptr_t>
using IndexedTriangleMesh2d = IndexedTriangleMesh<vec2, AttrT1, AttrT2, AttrT3>;

template <typename AttrT1 = nullptr_t, typename AttrT2 = nullptr_t, typename AttrT3 = nullptr_t>
using IndexedTriangleMesh3d = IndexedTriangleMesh<vec3, AttrT1, AttrT2, AttrT3>;

// Print IndexedTriangleMesh on ostream.

template <typename AttrT>
void IndexedTriangleMeshPrintAttribute(std::ostream& str, const std::vector<AttrT>& attributes,
                                       size_t index, const char* prefix) {
  str << prefix << attributes[index];
}
template <>
inline void IndexedTriangleMeshPrintAttribute(std::ostream& str,
                                              const std::vector<nullptr_t>& attributes,
                                              size_t index, const char* prefix) {}
template <typename PositionT, typename AttrT1, typename AttrT2, typename AttrT3>
std::ostream& operator<<(std::ostream& str,
                         const IndexedTriangleMesh<PositionT, AttrT1, AttrT2, AttrT3>& mesh) {
  str << "IndexedTriangleMesh[indices: " << mesh.index_count()
      << " vertices:" << mesh.vertex_count() << "\n";
  for (size_t tri = 0; tri + 2 < mesh.index_count(); tri += 3) {
    uint32_t ind0 = mesh.indices[tri];
    uint32_t ind1 = mesh.indices[tri + 1];
    uint32_t ind2 = mesh.indices[tri + 2];
    str << "tri " << tri / 3 << ": " << ind0 << "," << ind1 << "," << ind2 << "    "
        << mesh.positions[ind0] << "," << mesh.positions[ind1] << "," << mesh.positions[ind2]
        << "\n";
  }
  for (size_t i = 0; i < mesh.vertex_count(); ++i) {
    str << "vert " << i << " pos: " << mesh.positions[i];
    IndexedTriangleMeshPrintAttribute(str, mesh.attributes1, i, "   attr1: ");
    IndexedTriangleMeshPrintAttribute(str, mesh.attributes2, i, "   attr2: ");
    IndexedTriangleMeshPrintAttribute(str, mesh.attributes3, i, "   attr3: ");
    str << "\n";
  }
  return str << "]";
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_MESH_INDEXED_TRIANGLE_MESH_H_
