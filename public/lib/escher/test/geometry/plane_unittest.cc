// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(ES-139): make GLM_FORCE_RADIANS the default.  We rely on this below for
// creating quaternions.
#define GLM_FORCE_RADIANS
#include <glm/gtc/quaternion.hpp>

#include "lib/escher/geometry/plane_ops.h"

#include "lib/escher/geometry/intersection.h"
#include "lib/escher/geometry/transform.h"
#include "lib/escher/geometry/type_utils.h"

#include "gtest/gtest.h"

namespace {

using namespace escher;

TEST(plane3, PointOnPlaneConstructor) {
  std::vector<float> vals{0, 1, -1, 2, -2, 3, -3, 4, -4};
  size_t vals_size = vals.size();
  for (size_t i = 0; i < vals_size; ++i) {
    for (size_t j = 0; j < vals_size; ++j) {
      for (size_t k = 1; k < vals_size; ++k) {
        vec3 pt(vals[i], vals[j], vals[k]);
        vec3 dir = glm::normalize(pt);

        // Verify that both constructors yield the same result for planes
        // through the origin.
        EXPECT_EQ(plane3(dir, 0), plane3(vec3(0, 0, 0), dir));

        // Verify that both constructors yield the same result for planes
        // passing through the chosen point
        plane3 plane_through_point(dir, glm::length(pt));
        plane3 plane_through_point2(pt, dir);
        EXPECT_EQ(plane_through_point.dir(), plane_through_point2.dir());
        EXPECT_NEAR(plane_through_point.dist(), plane_through_point2.dist(),
                    kEpsilon);

        // Pick 3 other points on the same plane, and verify that they result in
        // the same plane through the point.
        if (i != 0 && j != 0) {
          vec3 ortho1 = glm::normalize(vec3(-vals[j], vals[i], 0));
          vec3 ortho2 = glm::cross(ortho1, dir);
          plane3 p1(pt + ortho1, dir);
          plane3 p2(pt + ortho2, dir);
          plane3 p12(pt + ortho1 + ortho2, dir);
          EXPECT_NEAR(plane_through_point.dist(), p1.dist(), kEpsilon);
          EXPECT_NEAR(plane_through_point.dist(), p2.dist(), kEpsilon);
          EXPECT_NEAR(plane_through_point.dist(), p12.dist(), kEpsilon);
        }
      }
    }
  }
}

TEST(plane2, Clipping) {
  vec2 pt1(0.4f, 100.f);
  vec2 pt2(0.4f, -100.f);
  vec2 pt3(0.6f, 100.f);
  vec2 pt4(0.6f, -100.f);

  {
    // 0.4 is to the left of the plane and 0.6 is to the right.
    plane2 pl(vec2(1.f, 0.f), 0.5f);
    EXPECT_TRUE(PlaneClipsPoint(pl, pt1));
    EXPECT_TRUE(PlaneClipsPoint(pl, pt2));
    EXPECT_FALSE(PlaneClipsPoint(pl, pt3));
    EXPECT_FALSE(PlaneClipsPoint(pl, pt4));
  }

  {
    // Same plane, different orientation.
    plane2 pl(vec2(-1.f, 0.f), -0.5f);
    EXPECT_FALSE(PlaneClipsPoint(pl, pt1));
    EXPECT_FALSE(PlaneClipsPoint(pl, pt2));
    EXPECT_TRUE(PlaneClipsPoint(pl, pt3));
    EXPECT_TRUE(PlaneClipsPoint(pl, pt4));
  }

  {
    // Non-axis-aligned plane through the origin.
    plane2 pl(glm::normalize(vec2(1.f, -1.f)), 0);
    EXPECT_TRUE(PlaneClipsPoint(pl, pt1));
    EXPECT_FALSE(PlaneClipsPoint(pl, pt2));
    EXPECT_TRUE(PlaneClipsPoint(pl, pt3));
    EXPECT_FALSE(PlaneClipsPoint(pl, pt4));
  }

  {
    // Non-axis-aligned plane offset from the origin.
    plane2 pl(glm::normalize(vec2(1.f, -1.f)), 100.f);
    // Length of the projection of the plane vector on the coordinate axes (same
    // for both because the slope is -1).
    const float axis_project = sqrt(100.f * 100.f / 2);
    // Double |axis_project| because the plane tangent is vec2(-1.f, 1.f).
    const float axis_intersect = 2.f * axis_project;

    EXPECT_FALSE(PlaneClipsPoint(pl, vec2(axis_intersect * 1.01f, 0.f)));
    EXPECT_FALSE(PlaneClipsPoint(pl, vec2(0.f, axis_intersect * -1.01f)));
    EXPECT_FALSE(
        PlaneClipsPoint(pl, vec2(axis_project, -axis_project) * 1.01f));
    EXPECT_TRUE(PlaneClipsPoint(pl, vec2(axis_intersect * 0.99f, 0.f)));
    EXPECT_TRUE(PlaneClipsPoint(pl, vec2(0.f, axis_intersect * -0.99f)));
    EXPECT_TRUE(PlaneClipsPoint(pl, vec2(axis_project, -axis_project) * 0.99f));
  }
}

// Shorter version of 2D plane clipping.
TEST(plane3, Clipping) {
  const vec3 plane_vec = glm::normalize(vec3(-1.f, -1.f, -1.f));
  const float plane_distance = -100.f;
  const plane3 plane(plane_vec, plane_distance);

  const float axis_project = plane_vec.x * plane_distance;
  EXPECT_EQ(axis_project, plane_vec.y * plane_distance);
  EXPECT_EQ(axis_project, plane_vec.y * plane_distance);

  EXPECT_TRUE(PlaneClipsPoint(
      plane, vec3(axis_project, axis_project, axis_project) * 1.01f));
  EXPECT_FALSE(PlaneClipsPoint(
      plane, vec3(axis_project, axis_project, axis_project) * 0.99f));

  // Let's say that (1,1,1) is a point on the plane parallel to our plane (i.e.
  // same normal, but different distance to origin).  What is the point (x,0,0)
  // where the plane intersects the x-axis?  The vector to this point is
  // (x-1,-1,-1), and since this vector must be perpendicular to (1,1,1), their
  // dot product must equal zero.  Therefore x - 1 - 1 -1 == 0, so x == 3.
  // Since (1,1,1) isn't a point on our plane, but axis_project * (1,1,1) is, we
  // have the following:
  const float axis_intersect = 3 * axis_project;
  EXPECT_TRUE(PlaneClipsPoint(plane, vec3(axis_intersect, 0.f, 0.f) * 1.01f));
  EXPECT_TRUE(PlaneClipsPoint(plane, vec3(0.f, axis_intersect, 0.f) * 1.01f));
  EXPECT_TRUE(PlaneClipsPoint(plane, vec3(0.f, 0.f, axis_intersect) * 1.01f));
  EXPECT_FALSE(PlaneClipsPoint(plane, vec3(axis_intersect, 0.f, 0.f) * 0.99f));
  EXPECT_FALSE(PlaneClipsPoint(plane, vec3(0.f, axis_intersect, 0.f) * 0.99f));
  EXPECT_FALSE(PlaneClipsPoint(plane, vec3(0.f, 0.f, axis_intersect) * 0.99f));
}

// Helper function for Intersection tests.
template <typename PlaneT, typename VecT>
float TestPlaneIntersection(PlaneT plane, VecT pt1, VecT pt2) {
  VecT seg_vec = pt2 - pt1;
  const float t1 = IntersectLinePlane(pt1, seg_vec, plane);
  const float t2 = IntersectLinePlane(pt2, -seg_vec, plane);
  if (t1 == FLT_MAX || t2 == FLT_MAX) {
    EXPECT_EQ(t1, t2);
    return t1;
  }

  float seg_vec_length_squared = glm::dot(seg_vec, seg_vec);

  // TODO(ES-137): revisit kEpsilon fudge-factor.
  EXPECT_NEAR(1.f, t1 + t2, kEpsilon * seg_vec_length_squared);

  const VecT intersection = pt1 + t1 * (pt2 - pt1);
  const VecT plane_def_vec = plane.dir() * plane.dist();
  const VecT vec_on_plane = intersection - plane_def_vec;
  // TODO(ES-137): revisit kEpsilon fudge-factor.
  if (glm::dot(vec_on_plane, vec_on_plane) > kEpsilon * 10) {
    // If the distance is great enough, verify that the intersection really is
    // on the plane.
    // TODO(ES-137): revisit kEpsilon fudge-factor.
    EXPECT_LT(glm::dot(vec_on_plane, plane.dir()), kEpsilon * 100);
  }

  return t1;
}

TEST(plane2, Intersection) {
  // This is covered sufficiently by the 3D test, since IntersectLinePlane() is
  // implemented generically: it does not depend on the dimensionality of the
  // vector space.
}

TEST(plane3, Intersection) {
  // Generate a plethora of "should intersect" cases by geometric construction.
  for (float origin_dist = -400.f; origin_dist <= 400.f; origin_dist += 100.f) {
    for (float radians = 0.f; radians <= 2 * M_PI; radians += M_PI / 5.9) {
      const vec3 plane_normal =
          glm::normalize(vec3(cos(radians), sin(radians), 0.5f));
      const plane3 plane(plane_normal, origin_dist);
      const vec3 plane_origin = plane.dir() * plane.dist();
      const vec3 tangent(-plane.dir().y, plane.dir().x, 0.f);
      const vec3 bitangent_mix =
          0.5f * (tangent + glm::cross(plane.dir(), tangent));

      // Compute some points on the plane, and then use the plane normal to
      // generate some points off the plane.
      for (float on_plane_dist = -50.f; on_plane_dist < 50.f;
           on_plane_dist += 5.f) {
        vec3 point_on_plane = plane_origin + bitangent_mix * on_plane_dist;

        for (float dist_between_off_plane_points = -55.f;
             dist_between_off_plane_points <= 55.f;
             dist_between_off_plane_points += 10.f) {
          for (float straddle_factor = 0.1f; straddle_factor <= 0.9f;
               straddle_factor += 0.2f) {
            vec3 pt1 = point_on_plane + straddle_factor *
                                            dist_between_off_plane_points *
                                            plane.dir();
            vec3 pt2 = point_on_plane + (straddle_factor - 1.f) *
                                            dist_between_off_plane_points *
                                            plane.dir();

            // Finally, let's intersect some points with the plane.
            auto result = TestPlaneIntersection(plane, pt1, pt2);
            EXPECT_NE(result, FLT_MAX);
            EXPECT_LE(result, 1.f);
            EXPECT_GE(result, 0.f);
            vec3 intersection_point = pt1 + result * (pt2 - pt1);

            EXPECT_LT(glm::length(point_on_plane - intersection_point), 1.f);
          }
        }
      }
    }
  }
}

TEST(plane2, NonIntersection) {
  vec2 point_on_plane = vec2(40, 30);
  vec2 offset_vec = vec2(100, 20);
  plane2 plane(point_on_plane, glm::normalize(offset_vec));

  // Rotate |offset_vec| by 90 degrees.
  vec2 parallel_vec(-offset_vec.y, offset_vec.x);

  // Starting from a point known to not be on the plane, verify that the
  // parallel ray does not intersect the plane.
  vec2 pt2(vec2(30, -40));
  auto result = TestPlaneIntersection(plane, pt2, pt2 + parallel_vec);
  EXPECT_EQ(result, FLT_MAX);

  // Perturb the ray direction slightly; it should now intersect the plane.
  vec2 non_parallel_vec = parallel_vec + vec2(0, .1f);
  result = TestPlaneIntersection(plane, pt2, pt2 + non_parallel_vec);
  EXPECT_NE(result, FLT_MAX);

  // Compute the intersection point.  Since the ray and plane are nearly
  // parallel, the precision will not be high.  Still, relative to the magnitude
  // of the intersection point, it's not bad.
  vec2 intersection_point = pt2 + result * non_parallel_vec;
  EXPECT_NEAR(glm::dot(plane.dir(), intersection_point), plane.dist(),
              glm::length(intersection_point) / 10000000.f);
}

// Helper function for TEST(plane3, Transformation).
template <typename PlaneT>
void TestPlaneTransformation(const Transform& transform,
                             const std::vector<PlaneT>& planes,
                             std::vector<PlaneT>* output_planes_ptr) {
  using VecT = typename PlaneT::VectorType;

  // Easier to use a reference.
  auto& output_planes = *output_planes_ptr;
  output_planes = planes;
  FXL_DCHECK(output_planes.size() == planes.size());

  mat4 matrix = static_cast<mat4>(transform);
  TransformPlanes(matrix, output_planes.data(), output_planes.size());

  for (size_t i = 0; i < planes.size(); ++i) {
    vec4 homo_point_on_plane = Homo4(planes[i].dir() * planes[i].dist(), 1);
    vec4 transformed_homo_point = matrix * homo_point_on_plane;
    VecT transformed_point =
        VecT(transformed_homo_point) / transformed_homo_point.w;

    EXPECT_NEAR(0.f, PlaneDistanceToPoint(output_planes[i], transformed_point),
                kEpsilon * 100);
  }
}

TEST(plane3, Transformation) {
  // Choose some arbtrary planes to transform.
  std::vector<plane3> planes3(
      {plane3(glm::normalize(vec3(1, 1, 1)), -5.f),
       plane3(glm::normalize(vec3(1, 1, 1)), 5.f),
       plane3(glm::normalize(vec3(-1, 10, 100)), -15.f),
       plane3(glm::normalize(vec3(1, -10, -100)), -15.f)});
  std::vector<plane3> output_planes3;

  // To test plane2 in addition to plane3, we drop the z-coordinate and then
  // renormalize.
  std::vector<plane2> planes2;
  std::vector<plane2> output_planes2;
  for (auto& p : planes3) {
    planes2.push_back(plane2(glm::normalize(vec2(p.dir())), p.dist()));
  }

  for (float trans_x = -220.f; trans_x <= 220.f; trans_x += 110.f) {
    for (float trans_y = -220.f; trans_y <= 220.f; trans_y += 110.f) {
      for (float trans_z = -220.f; trans_z <= 220.f; trans_z += 110.f) {
        for (float scale = 0.5f; scale <= 4.f; scale *= 2.f) {
          for (float angle = 0.f; angle < M_PI; angle += M_PI / 2.9f) {
            // For 2D, test by rotating around Z-axis.
            TestPlaneTransformation(
                Transform(vec3(trans_x, trans_y, trans_z),
                          vec3(scale, scale, scale),
                          glm::angleAxis(angle, vec3(0, 0, 1))),
                planes2, &output_planes2);

            // For 3D, test by rotating off the Z-axis.
            TestPlaneTransformation(
                Transform(vec3(trans_x, trans_y, trans_z),
                          vec3(scale, scale, scale),
                          glm::angleAxis(angle, vec3(0, .4f, 1))),
                planes3, &output_planes3);
          }
        }
      }
    }
  }
}

}  // namespace
