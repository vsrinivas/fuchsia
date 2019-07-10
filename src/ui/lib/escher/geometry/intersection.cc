// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/geometry/intersection.h"

namespace escher {

bool IntersectRayBox(const escher::ray4& ray, const escher::BoundingBox& box,
                     Interval* out_interval) {
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

  if (t_min < 0) {
    if (t_max < 0) {
      return false;
    }
  }

  // out_min and out_max values are only valid if there is a hit.
  if (out_interval) {
    *out_interval = Interval(glm::max(0.f, t_min), t_max);
  }

  return true;
}

// Use the "inside-out" test where the intersection point between the ray and the plane
// that contains the triangle is tested against each of the triangles edges. If the
// hit-point is inside all three edges then the ray has intersected the triangle.
bool IntersectRayTriangle(const escher::ray4& ray, const glm::vec3& v0, const glm::vec3& v1,
                          const glm::vec3& v2, float* out_distance) {
  // Get ray components.
  const glm::vec3 orig(ray.origin);
  const glm::vec3 dir(ray.direction);

  // Get the normal vector for the triangle by computing the cross product
  // of two of its edges, and normalizing the result.
  glm::vec3 edge_1(v1 - v0);
  glm::vec3 edge_2(v2 - v0);
  glm::vec3 norm = glm::normalize(glm::cross(edge_1, edge_2));

  // Find the intersection point between the ray and the triangle's plane.
  // First check if the ray is parallel to the plane, in which case there
  // is no intersection. Do this by computing the dot product of the ray direction
  // with the normal. If it is 0, that indicates that the ray direction vector is
  // 90 degrees from the normal, meaning it is parallel to the plane.
  float dot_ray_norm = glm::dot(norm, dir);
  if (fabs(dot_ray_norm) < kEpsilon) {
    return false;
  }

  // Check to see if the triangle is behind the ray origin by doing a ray-plane
  // intersection test and seeing if the parameterized distance |t| is negative.
  float t = glm::dot(v0 - orig, norm) / dot_ray_norm;
  if (t < 0.f) {
    return false;
  }

  // Now we know that 1) the triangle is in front of the ray and 2) the ray intersections
  // the plane. So we can grab the intersection point.
  glm::vec3 point(ray.At(t));

  // Now we perform the "inside out" test with P, to see if it lies on the inside of all
  // three of the triangle's edges. This is a quick lambda function to do the edge testing
  // so that we don't have to rewrite the same code three times.
  auto IsInside = [&point, &norm](const glm::vec3& va, const glm::vec3& vb) {
    glm::vec3 edge = vb - va;
    glm::vec3 dist_p = point - va;
    glm::vec3 perpendicular = glm::cross(edge, dist_p);

    // If the normal and the perpendicular vector are facing the same direction, the point
    // is inside the edge.
    return glm::dot(norm, perpendicular) >= 0.f;
  };

  // Check edge 0.
  if (!IsInside(v0, v1)) {
    return false;
  }

  // Check edge 1.
  if (!IsInside(v1, v2)) {
    return false;
  }

  // Check edge 2.
  if (!IsInside(v2, v0)) {
    return false;
  }

  // The ray hit the triangle! Write out the distance before returning.
  if (out_distance) {
    *out_distance = t;
  }

  return true;
}

}  // namespace escher
