// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_GEOMETRY_TESSELLATION_H_
#define LIB_ESCHER_GEOMETRY_TESSELLATION_H_

#include <vector>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/types.h"

namespace escher {

// Tessellate a circle.  The coarsest circle (i.e. subdivisions == 0) is a
// square; increasing the number of subdivisions doubles the number of vertices.
MeshPtr NewCircleMesh(MeshBuilderFactory* factory, const MeshSpec& spec,
                      int subdivisions, vec2 center, float radius,
                      float offset_magnitude = 0.f);

// Tessellate a basic rectangle
MeshPtr NewSimpleRectangleMesh(MeshBuilderFactory* factory);

// Tessellate a rectangle with multiple vertices along the top and bottom edges.
// Increasing subdivisions by 1 doubles the number of vertices. If the spec
// has kPositionOffset, the top offset points up and the bottom points down.
MeshPtr NewRectangleMesh(MeshBuilderFactory* factory, const MeshSpec& spec,
                         int subdivisions, vec2 size,
                         vec2 top_left = vec2(0.f, 0.f),
                         float top_offset_magnitude = 0.f,
                         float bottom_offset_magnitude = 0.f);

// Tessellate a ring whose area is bounded by an inner and an outer circle.
// Increasing subdivisions by 1 doubles the number of vertices.  If the spec
// has kPositionOffset, the outer offset points outward (away from the center of
// the ring) and the inner offset points inward (toward the center of the ring).
MeshPtr NewRingMesh(MeshBuilderFactory* factory, const MeshSpec& spec,
                    int subdivisions, vec2 center, float outer_radius,
                    float inner_radius, float outer_offset_magnitude = 0.f,
                    float inner_offset_magnitude = 0.f);

// Tessellate a full-screen mesh.  The returned mesh has only position and UV
// coordinates.
MeshPtr NewFullScreenMesh(MeshBuilderFactory* factory);

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
// TODO(ES-32): the approach described above is wrong: the newly-inserted
// vertices are correct positions, but the all of the initial octahedron edges
// are left untouched.  The proper approach is to double the number of vertices
// at each subdivision level (and quadruple the triangle count) by splitting
// each edge at the mid-point.  However, doing this with an indexed mesh (i.e.
// without inserting two vertices for every edge, one at each half-edge) is
// non-trivial, especially without a traversal-friendly mesh representation such
// as Rossignac's "corner table".
MeshPtr NewSphereMesh(MeshBuilderFactory* factory, const MeshSpec& spec,
                      int subdivisions, vec3 center, float radius);

}  // namespace escher

#endif  // LIB_ESCHER_GEOMETRY_TESSELLATION_H_
