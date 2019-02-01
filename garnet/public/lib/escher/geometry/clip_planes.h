// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_GEOMETRY_CLIP_PLANES_H_
#define LIB_ESCHER_GEOMETRY_CLIP_PLANES_H_

#include <array>

#include "lib/escher/geometry/types.h"

namespace escher {

class BoundingBox;

// Encapsulate a number of planes that can be used to perform clipping, e.g.
// in the vertex shader.  Recall that a plane is defined by the equation:
//   Ax + By + Cz + D == 0
//
// The definition above needs to be extended in order to be used for clipping:
// any point which does not satisfy the equation is not on the plane, but does
// not distinguish between the two sides of the plane.  We must introduce this
// distinction in order to clip points that lie on one side of the plane but not
// the other.
//
// Following http://github.prideout.net/clip-planes, we represent an oriented
// plane as follows:
//  - (A,B,C) is a normalized vec3 that encodes the orientation of the plane.
//    Any point on the side of the plane indicated by this vector is not clipped
//  - D' is the oriented (i.e. possibly negative) distance from the origin to
//    the plane along the vector (A,B,C).
//  - Then, for any point on the plane, we satisfy the plane equation by setting
//    D == -D'
//
// Therefore we can determine whether a point (x,y,z,1) is clipped by taking the
// dot product with (A,B,C,D); it is clipped if the result is < 0.
struct ClipPlanes {
  // Construct a new set of clip planes from the bounding box such that
  // everything outside of the box is clipped.
  static ClipPlanes FromBox(const BoundingBox& box);

  static const size_t kNumPlanes = 6;
  std::array<vec4, kNumPlanes> planes;

  // Return true if the point is within the negative half-space of any of the
  // planes, and false if within the positive half-space of all planes (or
  // perhaps directly on one or more plane).
  bool ClipsPoint(const vec4& point) const;
  bool ClipsPoint(const vec3& point) const {
    return ClipsPoint(vec4(point, 1));
  }

  // All planes must have a normalized vec3.
  bool IsValid();
};

}  // namespace escher

#endif  // LIB_ESCHER_GEOMETRY_CLIP_PLANES_H_
