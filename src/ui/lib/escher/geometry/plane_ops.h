// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_GEOMETRY_PLANE_OPS_H_
#define SRC_UI_LIB_ESCHER_GEOMETRY_PLANE_OPS_H_

#include "src/ui/lib/escher/geometry/type_utils.h"
#include "src/ui/lib/escher/geometry/types.h"

namespace escher {

// Transform the world-space plane into object space.  NOTE: use the same matrix
// that you would to transform an object into world space.  This may seem
// counter-intuitive; here is the reasoning:
//
// In order to transform a plane in world-space, you multiply it by the
// transpose of the inverse of the transform matrix.  For example, see:
// https://stackoverflow.com/questions/7685495/transforming-a-3d-plane-using-a-4x4-matrix
// However, we don't want to move a plane in world space, we want to move it to
// object space.  To do this, we need the inverse of |model_to_world_matrix|.
// However, once we have that matrix, the first thing we would naively do is
// invert it again, then transpose it.  The two inversions cancel each other
// out, and we can also avoid the transpose (see comment below).
template <typename PlaneT>
PlaneT TransformPlane(const mat4& model_to_world_matrix, const PlaneT& plane) {
  // Instead of transposing the matrix, simply do the multiplication with the
  // vector on the left-hand side.
  vec4 v = Homo4(plane.dir(), -plane.dist()) * model_to_world_matrix;

  // Must renormalize in case of scaling.
  float normalization_factor = 1 / glm::length(vec3(v));
  v *= normalization_factor;

  // Shorten the homogeneous direction vector before passing it to plane
  // constructor.  In the case of plane2, use the specialized vec3 constructor
  // to further shorten the direction to vec2, and also ensure that the
  // z-coordinate is zero.
  return PlaneT(vec3(v), -v.w);
}

// Transform the world-space plane into object space.  This is an optimization
// of TransformPlane(): it computes the same result without needing a matrix
// multiplication.
template <typename PlaneT>
PlaneT TranslatePlane(const typename PlaneT::VectorType& model_to_world_vec,
                      const PlaneT& plane) {
  return PlaneT(plane.dir(),
                glm::dot(-model_to_world_vec, plane.dir()) + plane.dist());
}

// Transform the world-space plane into object space.  This is an optimization
// of TransformPlane(): it computes the same result without needing a matrix
// multiplication.
template <typename PlaneT>
PlaneT ScalePlane(float model_to_world_scale, const PlaneT& plane) {
  FXL_DCHECK(model_to_world_scale > kEpsilon);
  return PlaneT(plane.dir(), plane.dist() / model_to_world_scale);
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
// Epsilon controls the aggressiveness of the clipping. A higher epsilon
// means less aggressive clipping, with a minimum allowed value of 0.f.
template <typename PlaneT, typename VecT>
bool PlaneClipsPoint(const PlaneT& plane, const VecT& point,
                     const float_t& epsilon = 0.f) {
  FXL_CHECK(epsilon >= 0.f);
  return PlaneDistanceToPoint(plane, point) < -epsilon;
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_GEOMETRY_PLANE_OPS_H_
