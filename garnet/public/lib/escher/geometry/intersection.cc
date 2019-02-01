// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/geometry/intersection.h"

namespace escher {

bool IntersectRayBox(const escher::ray4& ray, const escher::BoundingBox& box,
                     float* out_distance) {
  // This algorithm is from "An Efficient and Robust Ray–Box Intersection
  // Algorithm" by Amy Williams et al. 2004. Division by zero is handled via
  // IEEE floating-point arithmetic. See paper for details.
  //
  // Fundamentally (leaving aside optimizations), the algorithm projects the box
  // onto each coordinate axis and then computes the min/max parameters for the
  // ray segment that has the same projection onto the same axis. If the
  // intersection of these parameter ranges is empty, then the ray does not
  // intersect the box.  Otherwise, the minimum value of the intersected
  // parameter ranges gives the intersection point.
  const float idx = 1 / ray.direction.x;
  const float idy = 1 / ray.direction.y;
  const float idz = 1 / ray.direction.z;

  float t_min, t_max, ty_min, ty_max, tz_min, tz_max;

  // Bootstrap with x. Any coordinate axis would work just as well.
  t_min = ((idx < 0 ? box.max() : box.min()).x - ray.origin.x) * idx;
  t_max = ((idx < 0 ? box.min() : box.max()).x - ray.origin.x) * idx;

  ty_min = ((idy < 0 ? box.max() : box.min()).y - ray.origin.y) * idy;
  ty_max = ((idy < 0 ? box.min() : box.max()).y - ray.origin.y) * idy;

  if (t_min > ty_max || ty_min > t_max) {
    // The parameter ranges of the "x-axis projection" and "y-axis projection"
    // ray segments are disjoint. Therefore the ray does not intersect the box.
    return false;
  }

  // Compute the intersection of the two parameter ranges.
  if (ty_min > t_min)
    t_min = ty_min;
  if (ty_max < t_max)
    t_max = ty_max;

  tz_min = ((idz < 0 ? box.max() : box.min()).z - ray.origin.z) * idz;
  tz_max = ((idz < 0 ? box.min() : box.max()).z - ray.origin.z) * idz;

  if (t_min > tz_max || tz_min > t_max)
    return false;

  if (tz_min > t_min)
    t_min = tz_min;
  if (tz_max < t_max)
    t_max = tz_max;

  *out_distance = t_min;

  if (*out_distance < 0) {
    *out_distance = t_max;
    if (*out_distance < 0)
      return false;
  }

  return true;
}

}  // namespace escher