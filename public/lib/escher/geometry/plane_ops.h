// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_GEOMETRY_PLANE_OPS_H_
#define LIB_ESCHER_GEOMETRY_PLANE_OPS_H_

#include "lib/escher/geometry/type_utils.h"
#include "lib/escher/geometry/types.h"

namespace escher {

// Primarily a helper function for TransformPlanes(), this may also be useful if
// the caller already has a transposed inverse matrix available.
template <typename PlaneT>
PlaneT TransformPlaneUsingTransposedInverse(
    const mat4& transposed_inverse_matrix, const PlaneT& plane) {
  // This is a slight misuse of Homo4, but it allows us to properly extend both
  // vec2 and vec3 to vec4.
  vec4 v = transposed_inverse_matrix * Homo4(plane.dir(), -plane.dist());

  // Must renormalize in case of scaling.
  float normalization_factor = 1 / glm::length(vec3(v));
  v *= normalization_factor;

  // Shorten the homogeneous direction vector before passing it to plane
  // constructor.  In the case of plane2, use the specialized vec3 constructor
  // to further shorten the direction to vec2, and also ensure that the
  // z-coordinate is zero.
  return PlaneT(vec3(v), -v.w);
}

// Transform the planes in-place by the specified matrix.  The reason the API
// transforms multiple planes at once is that it requires the transposed inverse
// of |m|, which we want to avoid computing individually for each plane.
template <typename PlaneT>
void TransformPlanes(const mat4& m, PlaneT* planes, size_t num_planes) {
  mat4 matrix = glm::transpose(glm::inverse(m));
  for (size_t i = 0; i < num_planes; ++i) {
    planes[i] = TransformPlaneUsingTransposedInverse(matrix, planes[i]);
  }
}

// Return the distance from the point to the plane.  This distance is oriented:
// it can be positive or negative (or zero, if the point is on the plane).  A
// positive value means that the point is inside the half-space defined by the
// plane, and a negative value means that the point is outside.
template <typename VecT>
float PlaneDistanceToPoint(const planeN<VecT>& plane, const VecT& point) {
  return glm::dot(plane.dir(), point) - plane.dist();
}

// Promote |point| to 3D in order to be tested against 3D plane.
inline float PlaneDistanceToPoint(const planeN<vec3>& plane,
                                  const vec2& point) {
  return PlaneDistanceToPoint(plane, vec3(point, 0));
}

// Demote |point| to 2D in order to be tested against 2D plane.
inline float PlaneDistanceToPoint(const planeN<vec2>& plane,
                                  const vec3& point) {
  return PlaneDistanceToPoint(plane, vec2(point));
}

// Return true if point is contained within the half-space of the oriented
// plane, or on the boundary.
template <typename PlaneT, typename VecT>
bool PlaneClipsPoint(const PlaneT& plane, const VecT& point) {
  return PlaneDistanceToPoint(plane, point) < 0.f;
}

}  // namespace escher

#endif  // LIB_ESCHER_GEOMETRY_PLANE_OPS_H_
