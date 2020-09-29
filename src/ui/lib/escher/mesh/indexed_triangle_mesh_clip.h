// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_MESH_INDEXED_TRIANGLE_MESH_CLIP_H_
#define SRC_UI_LIB_ESCHER_MESH_INDEXED_TRIANGLE_MESH_CLIP_H_

#include <unordered_map>
#include <utility>
#include <vector>

#include "src/ui/lib/escher/geometry/intersection.h"
#include "src/ui/lib/escher/geometry/plane_ops.h"
#include "src/ui/lib/escher/math/lerp.h"
#include "src/ui/lib/escher/mesh/indexed_triangle_mesh.h"
#include "src/ui/lib/escher/util/bitmap.h"
#include "src/ui/lib/escher/util/pair_hasher.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {

// IndexedTriangleMeshClip() generates the output mesh resulting from
// iteratively clipping the input mesh against a list of input planes; the input
// to each iteration is the output of the previous iteration.
//
// Algorithm overview (simplified):
// - for each plane:
//   - for each vertex:
//     - set bit if vertex is clipped by current plane
//   - if no vertices are clipped, proceed to next plane
//   - otherwise, for each triangle:
//     - if 0 vertices are clipped, the triangle is copied to the output mesh
//     - if 3 vertices are clipped, no triangle is added to the output mesh
//     - if 1 or 2 vertices are clipped, then two new vertices are generated
//       where the triangle edges intersect the plane.
//       - if 2 vertices are clipped, the resulting triangle consists of the
//         unclipped tip of the triangle + the two new edge vertices.
//       - if 1 vertex is clipped, the result is a quad consisting of the two
//         unclipped vertices + the two new edge vertices.  This quad is split
//         diagonally into two triangles, which are added to the output mesh.
//
// The implementation is slightly more complicated than the simplified overview
// above.  The extra complexity is mostly to avoid generating redundant vertex
// data, by ensuring that indices are reused when multiple triangles share the
// same vertices.
template <typename MeshT, typename PlaneT>
auto IndexedTriangleMeshClip(MeshT input_mesh, const std::vector<PlaneT>& planes)
    -> std::pair<MeshT, std::vector<PlaneT>> {
  return IndexedTriangleMeshClip<MeshT, PlaneT>(std::move(input_mesh), planes.data(),
                                                planes.size());
}

// Helper functions that interpolate two attributes from a source mesh, and push
// the interpolated value to a target mesh.
template <typename AttrT>
void IndexedTriangleMeshPushLerpedAttribute(std::vector<AttrT>* target,
                                            std::vector<AttrT>* source_ptr, size_t index1,
                                            size_t index2, float interp_param) {
  auto& source = *source_ptr;
  target->push_back(Lerp(source[index1], source[index2], interp_param));
}
template <>
inline void IndexedTriangleMeshPushLerpedAttribute(std::vector<nullptr_t>* target,
                                                   std::vector<nullptr_t>* source, size_t index1,
                                                   size_t index2, float interp_param) {}
template <typename MeshT>
void IndexedTriangleMeshPushLerpedAttributes(MeshT* target, MeshT* source, size_t index1,
                                             size_t index2, float interp_param) {
  IndexedTriangleMeshPushLerpedAttribute(&target->positions, &source->positions, index1, index2,
                                         interp_param);
  IndexedTriangleMeshPushLerpedAttribute(&target->attributes1, &source->attributes1, index1, index2,
                                         interp_param);
  IndexedTriangleMeshPushLerpedAttribute(&target->attributes2, &source->attributes2, index1, index2,
                                         interp_param);
  IndexedTriangleMeshPushLerpedAttribute(&target->attributes3, &source->attributes3, index1, index2,
                                         interp_param);
}

// Helper functions that copy an attribute from a source mesh, pushing it to
// the back of a target mesh.
template <typename AttrT>
void IndexedTriangleMeshPushCopiedAttribute(std::vector<AttrT>* target, std::vector<AttrT>* source,
                                            size_t index) {
  target->push_back((*source)[index]);
}
template <>
inline void IndexedTriangleMeshPushCopiedAttribute(std::vector<nullptr_t>* target,
                                                   std::vector<nullptr_t>* source, size_t index) {}
template <typename MeshT>
void IndexedTriangleMeshPushCopiedAttributes(MeshT* target, MeshT* source, size_t index) {
  IndexedTriangleMeshPushCopiedAttribute(&target->positions, &source->positions, index);
  IndexedTriangleMeshPushCopiedAttribute(&target->attributes1, &source->attributes1, index);
  IndexedTriangleMeshPushCopiedAttribute(&target->attributes2, &source->attributes2, index);
  IndexedTriangleMeshPushCopiedAttribute(&target->attributes3, &source->attributes3, index);
}

template <typename MeshT, typename PlaneT>
auto IndexedTriangleMeshClip(MeshT input_mesh, const PlaneT* planes, size_t num_planes)
    -> std::pair<MeshT, std::vector<PlaneT>> {
  TRACE_DURATION("gfx", "escher::IndexedTriangleMeshClip", "triangles", input_mesh.triangle_count(),
                 "vertices", input_mesh.vertex_count(), "num_planes", num_planes);
  FX_DCHECK(input_mesh.IsValid());
  using Edge = typename MeshT::EdgeType;
  using Index = typename MeshT::IndexType;
  using Position = typename MeshT::PositionType;

  // Stores the output result.
  std::pair<MeshT, std::vector<PlaneT>> result;
  auto& output_mesh = result.first;
  auto& output_planes = result.second;

  // This should be a safe over-allocation: it would be very difficult for the
  // number of vertices in the clipped mesh to be > double that of the input
  // mesh.
  BitmapWithStorage clipped_vertices;
  clipped_vertices.SetSize(static_cast<uint32_t>(input_mesh.positions.size() * 2));
  // Keeps track of whether the previous plane clipped any vertices.
  bool plane_clipped_vertices = false;

  // Storage for |remapped_index_for_unclipped_vertex| and
  // |get_index_for_split_edge_vertex| closures, below.  Outside the loop so
  // that the memory can be reused between iterations.
  std::unordered_map<Index, Index> reordered_indices;
  std::unordered_map<Edge, Index, PairHasher> new_edge_vertex_indices;

  for (size_t plane_index = 0; plane_index < num_planes; ++plane_index) {
    TRACE_DURATION("gfx", "escher::IndexedTriangleMeshClip[loop]", "plane_index", plane_index);
    auto& plane = planes[plane_index];

    // If the plane from the previous pass clipped any vertices, then the output
    // from the previous pass becomes the input to this pass.  Also, clear the
    // output and temp data in preparation for this pass (without releasing any
    // capacity that it may have already allocated).
    if (plane_clipped_vertices) {
      input_mesh.clear();
      std::swap(input_mesh, output_mesh);

      plane_clipped_vertices = false;
      clipped_vertices.ClearAll();
      if (input_mesh.positions.size() > clipped_vertices.GetSize()) {
        clipped_vertices.SetSize(static_cast<uint32_t>(input_mesh.positions.size() * 2));
      }
      reordered_indices.clear();
      new_edge_vertex_indices.clear();
    }

    // Mark all the vertices that are clipped by the current plane.
    const size_t num_input_vertices = input_mesh.positions.size();
    {
      TRACE_DURATION("gfx", "escher::IndexedTriangleMeshClip[clip_verts]");
      for (uint32_t i = 0; i < num_input_vertices; ++i) {
        // Don't bother clipping if the point is very close to the plane.
        if (PlaneDistanceToPoint(plane, input_mesh.positions[i]) < -kEpsilon) {
          clipped_vertices.Set(i);
          plane_clipped_vertices = true;
        }
      }
    }
    if (!plane_clipped_vertices) {
      // No vertices were clipped by the current plane, so the mesh is
      // unchanged.  Continue on to the next clip-plane.
      //
      // NOTE: we might consider tracking the number of clipped vertices and
      // returning and empty mesh immediately if all vertices are clipped.  The
      // resulting speedup would be minimal, because the current code will set
      // |plane_clipped_vertices| to false in all subsequent loop iterations,
      // and therefore quickly return an empty mesh anyway.
      continue;
    }
    // The plane clipped at least one vertex, so we must iterate through the
    // triangles to generate a clipped mesh.
    output_planes.push_back(plane);

    // Helper closure.
    // For each plane where at least one vertex is clipped, a new output mesh is
    // generated.  As we iterate over the triangles of the input mesh, the first
    // time an unclipped vertex is encountered, we copy/append its data to the
    // output mesh, and map the input index to the new highest index of the
    // output mesh.  When the same input index is seen again, the corresponding
    // output index is returned.
    auto remapped_index_for_unclipped_vertex = [&reordered_indices, &input_mesh,
                                                &output_mesh](Index original_index) -> Index {
      auto it = reordered_indices.find(original_index);
      if (it != reordered_indices.end()) {
        // The input vertex was already seen, so return the index of the
        // corresponding output vertex.
        return it->second;
      }
      // The input vertex was not previously seen, so we:
      // - copy/append the vertex data to the output mesh
      // - map the input index to the corresponding index of the output mesh
      Index new_index = static_cast<Index>(output_mesh.positions.size());
      FX_DCHECK(original_index < input_mesh.vertex_count());
      IndexedTriangleMeshPushCopiedAttributes(&output_mesh, &input_mesh, original_index);
      reordered_indices.insert(it, {original_index, new_index});
      return new_index;
    };

    // Helper closure.
    // For each plane where at least one vertex is clipped, a new output mesh is
    // generated.  Whenever there is also at least one vertex that is not
    // clipped, it means that there is at least one triangle edge that
    // intersects the current plane.  When this occurs, a new vertex is
    // generated with the appropriate position and interpolated attribute
    // values.  Because it is common for adjacent triangles to share an edge,
    // we establish a mapping from this edge to the index of the newly-generated
    // vertex; when the same edge is seen in a subsequent triangle, we simply
    // return the index instead of generating a new interpolated vertex.
    auto get_index_for_split_edge_vertex = [&new_edge_vertex_indices, &plane, &input_mesh,
                                            &output_mesh](Edge edge) -> Index {
      // Use canonical sorting of edge indices so that the split-vertex can be
      // found regardless of the orientation of the edge.
      if (edge.first > edge.second) {
        std::swap(edge.first, edge.second);
      }

      // If this edge has already been split, return the index of the
      // previously-generated vertex.
      auto it = new_edge_vertex_indices.find(edge);
      if (it != new_edge_vertex_indices.end()) {
        return it->second;
      }

      // This edge has not previously been encountered, so we generate a new
      // vertex at the point of intersection with the plane.
      const Position edge_origin = input_mesh.positions[edge.first];
      const Position edge_vector = input_mesh.positions[edge.second] - edge_origin;
      float t = IntersectLinePlane(edge_origin, edge_vector, plane);
      if (t == FLT_MAX) {
        // Since |get_index_for_split_edge_vertex| is only called when one of
        // the edge vertices is clipped and the other is not, there should
        // always be an intersection.  However, IntersectLinePlane() takes a
        // conservative approach to avoid computing a wildly erroneous
        // intersection position due to numerical instability.  Since we don't
        // know where the intersection actually is, assume it is at the midpoint
        // of the two edge vertices.
        t = 0.5f;
      }
      const Index new_index = output_mesh.vertex_count();
      IndexedTriangleMeshPushLerpedAttributes(&output_mesh, &input_mesh, edge.first, edge.second,
                                              t);

      // Cache the index in case a subsequent triangle shares the same edge.
      new_edge_vertex_indices.insert(it, {edge, new_index});
      return new_index;
    };

    // For each triangle, handle the four cases:
    // - all vertices are clipped by the plane
    // - no vertices are clipped by the plane
    // - one vertex is clipped by the plane, resulting in a quadrilateral
    // - two vertices are clipped by the plane, resulting in a triangle
    const size_t input_index_count = input_mesh.index_count();
    FX_DCHECK(input_index_count % 3 == 0);

    for (size_t i = 0; i + 2 < input_index_count; i += 3) {
      Index* tri = input_mesh.indices.data() + i;

      const bool v0_clipped = clipped_vertices.Get(tri[0]);
      const bool v1_clipped = clipped_vertices.Get(tri[1]);
      const bool v2_clipped = clipped_vertices.Get(tri[2]);
      const int clipped_count = (v0_clipped ? 1 : 0) + (v1_clipped ? 1 : 0) + (v2_clipped ? 1 : 0);
      switch (clipped_count) {
        case 0: {
          // This triangle is completely unclipped.  All vertices are copied
          // directly to the output mesh (albeit with possibly-remapped
          // indices).
          output_mesh.indices.push_back(remapped_index_for_unclipped_vertex(tri[0]));
          output_mesh.indices.push_back(remapped_index_for_unclipped_vertex(tri[1]));
          output_mesh.indices.push_back(remapped_index_for_unclipped_vertex(tri[2]));
        } break;
        case 1: {
          // A single vertex was clipped from the triangle, resulting in a
          // quadrilateral consisting of the two unclipped vertices and the
          // two new vertices resulting from the intersection of the plane
          // with the triangle.
          Index clipped_tip = v0_clipped ? 0 : (v1_clipped ? 1 : 2);

          // Obtain the indices of the two new vertices from the intersected
          // edges, in the normal winding order.  Then, add them as the first
          // two vertices in the next triangle (some more work will be required
          // to determine the triangle's final vertex: there are two ways we can
          // split the quad).
          Index edge_index_1 =
              get_index_for_split_edge_vertex({tri[clipped_tip], tri[(clipped_tip + 2) % 3]});
          Index edge_index_2 =
              get_index_for_split_edge_vertex({tri[clipped_tip], tri[(clipped_tip + 1) % 3]});
          output_mesh.indices.push_back(edge_index_1);
          output_mesh.indices.push_back(edge_index_2);

          // Before adding the final vertex of the initial triangle, we must
          // decide which diagonal to use to split the quad.  We pick the
          // shorter diagonal, with the intention of minimizing long, skinny
          // triangles.
          Position vector_from_edge_index_1 = input_mesh.positions[tri[(clipped_tip + 1) % 3]] -
                                              output_mesh.positions[edge_index_1];
          Position vector_from_edge_index_2 = input_mesh.positions[tri[(clipped_tip + 2) % 3]] -
                                              output_mesh.positions[edge_index_2];

          if (glm::dot(vector_from_edge_index_1, vector_from_edge_index_1) <
              glm::dot(vector_from_edge_index_2, vector_from_edge_index_2)) {
            // The quad-diagonal originating from edge_index_1 is the shorter of
            // the two, so split quad along that diagonal.
            Index diagonal_index = remapped_index_for_unclipped_vertex(tri[(clipped_tip + 1) % 3]);
            output_mesh.indices.push_back(diagonal_index);

            // Now we also know the indices for the other triangle.
            output_mesh.indices.push_back(edge_index_1);
            output_mesh.indices.push_back(diagonal_index);
            output_mesh.indices.push_back(
                remapped_index_for_unclipped_vertex(tri[(clipped_tip + 2) % 3]));
          } else {
            // Split along the diagonal originating from edge_index_2.  This is
            // the shorter of the two diagonals, see above.
            Index diagonal_index = remapped_index_for_unclipped_vertex(tri[(clipped_tip + 2) % 3]);
            output_mesh.indices.push_back(diagonal_index);

            // Now we also know the indices for the other triangle.
            output_mesh.indices.push_back(edge_index_2);
            output_mesh.indices.push_back(
                remapped_index_for_unclipped_vertex(tri[(clipped_tip + 1) % 3]));
            output_mesh.indices.push_back(diagonal_index);
          }
        } break;
        case 2: {
          // Two vertices were clipped from the triangle, leaving a smaller
          // "tip" triangle.  We keep the tip vertex, and generate two new
          // vertices by intersecting the plane with the two edges incident to
          // the unclipped vertex.  Note that since most edges are shared
          // between 2 triangles, one or both of these vertices may already
          // have been generated when clipping other triangles; in this case
          // we simply reference the already-generated vertex by its index.

          // Determine which of the triangle vertices is the unclipped tip.
          Index unclipped_tip = v0_clipped ? (v1_clipped ? 2 : 1) : 0;

          output_mesh.indices.push_back(remapped_index_for_unclipped_vertex(tri[unclipped_tip]));
          output_mesh.indices.push_back(
              get_index_for_split_edge_vertex({tri[unclipped_tip], tri[(unclipped_tip + 1) % 3]}));
          output_mesh.indices.push_back(
              get_index_for_split_edge_vertex({tri[unclipped_tip], tri[(unclipped_tip + 2) % 3]}));
        } break;
        default:
          // This triangle is completely clipped; move on to the next
          // triangle.
          FX_DCHECK(clipped_count == 3);
      }
      // Proceed to next triangle.
    }
  }

  // If the final plane did not clip any vertices (or if there were no planes),
  // then we need to move the input into the result.
  if (!plane_clipped_vertices) {
    result.first = std::move(input_mesh);
  }
  FX_DCHECK(result.first.index_count() % 3 == 0);
  return result;
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_MESH_INDEXED_TRIANGLE_MESH_CLIP_H_
