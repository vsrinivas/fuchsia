// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_GEOMETRY_INTERSECTION_H_
#define SRC_UI_LIB_ESCHER_GEOMETRY_INTERSECTION_H_

#include <cfloat>

#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/geometry/types.h"

namespace escher {

// Returns whether a ray intersects an axis-aligned bounding box. Upon return,
// |out_distance| contains the distance from the ray origin to the intersection
// point in units of ray length.
bool IntersectRayBox(const escher::ray4& ray, const escher::BoundingBox& box,
                     float* out_distance);

// Return the distance from the ray origin to the intersection point in units
// of ray length, or FLT_MAX if the line and plane are (nearly) parallel.  This
// can be used to test line, ray, and line-segment intersection:
//   - lines intersect the plane when the return value is < FLT_MAX.
//   - rays intersect when the return value is >= 0 and < FLT_MAX.
//   - line segments intersect when the return value is >= 0 and <= 1.
template <typename VecT>
float IntersectLinePlane(const VecT& ray_origin, const VecT& ray_direction,
                         const planeN<VecT>& plane) {
  float denominator = glm::dot(ray_direction, plane.dir());
  return std::abs(denominator) <
                 kEpsilon * glm::dot(ray_direction, ray_direction)
             ? FLT_MAX
             : (plane.dist() - glm::dot(ray_origin, plane.dir())) / denominator;
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_GEOMETRY_INTERSECTION_H_
