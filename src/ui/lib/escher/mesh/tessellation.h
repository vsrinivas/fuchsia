// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_MESH_TESSELLATION_H_
#define SRC_UI_LIB_ESCHER_MESH_TESSELLATION_H_

#include <vector>

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/mesh/indexed_triangle_mesh.h"

namespace escher {

// Tessellate a circle.  The coarsest circle (i.e. subdivisions == 0) is a
// square; increasing the number of subdivisions doubles the number of vertices.
MeshPtr NewCircleMesh(MeshBuilderFactory* factory, BatchGpuUploader* gpu_uploader,
                      const MeshSpec& spec, int subdivisions, vec2 center, float radius,
                      float offset_magnitude = 0.f);

// Tessellate a circle and return it as an IndexedTriangleMesh suitable for
// further processing on the CPU.
IndexedTriangleMesh2d<vec2> NewCircleIndexedTriangleMesh(const MeshSpec& spec,
                                                         uint32_t subdivisions, vec2 center,
                                                         float radius);

// Tessellate a rectangle with multiple vertices along the top and bottom edges.
// Increasing subdivisions by 1 doubles the number of vertices. If the spec
// has kPositionOffset, the top offset points up and the bottom points down.
MeshPtr NewRectangleMesh(MeshBuilderFactory* factory, BatchGpuUploader* gpu_uploader,
                         const MeshSpec& spec, int subdivisions, vec2 extent,
                         vec2 top_left = vec2(0.f, 0.f), float top_offset_magnitude = 0.f,
                         float bottom_offset_magnitude = 0.f);

// Tessellate a ring whose area is bounded by an inner and an outer circle.
// Increasing subdivisions by 1 doubles the number of vertices.  If the spec
// has kPositionOffset, the outer offset points outward (away from the center of
// the ring) and the inner offset points inward (toward the center of the ring).
MeshPtr NewRingMesh(MeshBuilderFactory* factory, BatchGpuUploader* gpu_uploader,
                    const MeshSpec& spec, int subdivisions, vec2 center, float outer_radius,
                    float inner_radius, float outer_offset_magnitude = 0.f,
                    float inner_offset_magnitude = 0.f);

// Tessellate a full-screen mesh.  The returned mesh has only position and UV
// coordinates.
MeshPtr NewFullScreenMesh(MeshBuilderFactory* factory, BatchGpuUploader* gpu_uploader);

// Tessellate a sphere with the specified center and radius.  If subdivisions ==
// 0, the result is a regular octahedron.  Increasing the number of subdivisions
// by 1 subdivides each triangle into 3 by adding a vertex at the triangle
// midpoint, and moving it outward to match the desired radius.
//
// If UV-coordinates are to be generated, the surface is parameterized as
// follows.  Looking at the un-subdivided octahedron from the right (i.e. in the
// direction of the negative X-axis), the 4 visible faces are mapped to the
// rotated square with corners (0, .5), (.5, 0), (1, .5), (.5, 1), and the
// vertex at (radius, 0, 0) is mapped to the center of the texture: (.5, .5).
// The unmapped 4 corners of the texture are "folded over" to map to the 4
// hidden faces of the octahedron.  During subdivision, the UV coordinates are
// linearly interpolated for each new vertex.
//
// TODO(fxbug.dev/7329): the approach described above is wrong: the newly-inserted
// vertices are correct positions, but the all of the initial octahedron edges
// are left untouched.  The proper approach is to double the number of vertices
// at each subdivision level (and quadruple the triangle count) by splitting
// each edge at the mid-point.  However, doing this with an indexed mesh (i.e.
// without inserting two vertices for every edge, one at each half-edge) is
// non-trivial, especially without a traversal-friendly mesh representation such
// as Rossignac's "corner table".
MeshPtr NewSphereMesh(MeshBuilderFactory* factory, BatchGpuUploader* gpu_uploader,
                      const MeshSpec& spec, int subdivisions, vec3 center, float radius);

// This returns a cube shaped mesh, with it's min-bounds point at the origin.
// To get boxes of different dimensions, this mesh can just be scaled in a
// non-uniform manner in the (x,y,z) directions with its transformation matrix.
IndexedTriangleMesh3d<vec2> NewCubeIndexedTriangleMesh(const MeshSpec& spec);

// Tessellate a basic rectangle on the XY plane with no depth. The origin refers
// to the top-left hand corner of rectangle, and the extent is the width and height.
// UV coordinates are also provided directly by the caller.
IndexedTriangleMesh2d<vec2> NewFlatRectangleMesh(vec2 origin, vec2 extent,
                                                 vec2 top_left_uv = vec2(0, 0),
                                                 vec2 bottom_right_uv = vec2(1, 1));

// The following functions are used for convenience during unit testing.
IndexedTriangleMesh2d<vec2> GetStandardTestMesh2d();
IndexedTriangleMesh3d<vec2> GetStandardTestMesh3d();

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_MESH_TESSELLATION_H_
