// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/7230): make GLM_FORCE_RADIANS the default.  We rely on this below for
// creating quaternions.
#define GLM_FORCE_RADIANS
#include <gtest/gtest.h>

#include "src/ui/lib/escher/geometry/intersection.h"
#include "src/ui/lib/escher/geometry/plane_ops.h"
#include "src/ui/lib/escher/geometry/transform.h"
#include "src/ui/lib/escher/geometry/type_utils.h"

#include <glm/gtc/quaternion.hpp>

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
        EXPECT_NEAR(plane_through_point.dist(), plane_through_point2.dist(), kEpsilon);

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
    EXPECT_FALSE(PlaneClipsPoint(pl, vec2(axis_project, -axis_project) * 1.01f));
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

  EXPECT_TRUE(PlaneClipsPoint(plane, vec3(axis_project, axis_project, axis_project) * 1.01f));
  EXPECT_FALSE(PlaneClipsPoint(plane, vec3(axis_project, axis_project, axis_project) * 0.99f));

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

  // TODO(fxbug.dev/7228): revisit kEpsilon fudge-factor.
  EXPECT_NEAR(1.f, t1 + t2, kEpsilon * seg_vec_length_squared);

  const VecT intersection = pt1 + t1 * (pt2 - pt1);
  const VecT plane_def_vec = plane.dir() * plane.dist();
  const VecT vec_on_plane = intersection - plane_def_vec;
  // TODO(fxbug.dev/7228): revisit kEpsilon fudge-factor.
  if (glm::dot(vec_on_plane, vec_on_plane) > kEpsilon * 10) {
    // If the distance is great enough, verify that the intersection really is
    // on the plane.
    // TODO(fxbug.dev/7228): revisit kEpsilon fudge-factor.
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
      const vec3 plane_normal = glm::normalize(vec3(cos(radians), sin(radians), 0.5f));
      const plane3 plane(plane_normal, origin_dist);
      const vec3 plane_origin = plane.dir() * plane.dist();
      const vec3 tangent(-plane.dir().y, plane.dir().x, 0.f);
      const vec3 bitangent_mix = 0.5f * (tangent + glm::cross(plane.dir(), tangent));

      // Compute some points on the plane, and then use the plane normal to
      // generate some points off the plane.
      for (float on_plane_dist = -50.f; on_plane_dist < 50.f; on_plane_dist += 5.f) {
        vec3 point_on_plane = plane_origin + bitangent_mix * on_plane_dist;

        for (float dist_between_off_plane_points = -55.f; dist_between_off_plane_points <= 55.f;
             dist_between_off_plane_points += 10.f) {
          for (float straddle_factor = 0.1f; straddle_factor <= 0.9f; straddle_factor += 0.2f) {
            vec3 pt1 =
                point_on_plane + straddle_factor * dist_between_off_plane_points * plane.dir();
            vec3 pt2 = point_on_plane +
                       (straddle_factor - 1.f) * dist_between_off_plane_points * plane.dir();

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
void TestPlaneTransformation(const Transform& transform, const std::vector<PlaneT>& planes) {
  using VecT = typename PlaneT::VectorType;

  // The planes start in world-space, and are transformed into object-space.
  mat4 matrix = static_cast<mat4>(transform);
  std::vector<PlaneT> output_planes;
  for (auto& p : planes) {
    output_planes.push_back(TransformPlane(matrix, p));
  }

  // Synthesize a grid of points in object-space and verify that their
  // distances from the object-space plane are the same as transforming them
  // into world-space and testing against the world-space plane.
  for (float pt_x = -17.5f; pt_x < 20.f; pt_x += 5.f) {
    for (float pt_y = -17.5f; pt_y < 20.f; pt_y += 5.f) {
      for (float pt_z = -17.5f; pt_z < 20.f; pt_z += 5.f) {
        const VecT object_space_point(vec3(pt_x, pt_y, pt_z));
        const VecT world_space_point(matrix * Homo4(object_space_point, 1));

        for (size_t i = 0; i < planes.size(); ++i) {
          const PlaneT& world_space_plane = planes[i];
          const PlaneT& object_space_plane = output_planes[i];

          const float world_space_distance =
              PlaneDistanceToPoint(world_space_plane, world_space_point);
          const float object_space_distance =
              PlaneDistanceToPoint(object_space_plane, object_space_point);
          const float object_space_distance_scaled = object_space_distance * transform.scale.x;

          const float kFudgedEpsilon = kEpsilon * 1000.f;
          EXPECT_NEAR(world_space_distance, object_space_distance_scaled, kFudgedEpsilon);
        }
      }
    }
  }
}

// Test matrix transformation of world-space planes into object-space.
TEST(plane3, Transformation) {
  // Choose some arbtrary planes to transform.
  std::vector<plane3> planes3({plane3(glm::normalize(vec3(1, 1, 1)), -5.f),
                               plane3(glm::normalize(vec3(1, 1, 1)), 5.f),
                               plane3(glm::normalize(vec3(-1, 10, 100)), -15.f),
                               plane3(glm::normalize(vec3(1, -10, -100)), -15.f)});

  // To test plane2 in addition to plane3, we drop the z-coordinate and then
  // renormalize.
  std::vector<plane2> planes2;
  for (auto& p : planes3) {
    planes2.push_back(plane2(glm::normalize(vec2(p.dir())), p.dist()));
  }

  // Step through parameter-space to generate a large number of Transforms.
  for (float trans_x = -220.f; trans_x <= 220.f; trans_x += 110.f) {
    for (float trans_y = -220.f; trans_y <= 220.f; trans_y += 110.f) {
      for (float trans_z = -220.f; trans_z <= 220.f; trans_z += 110.f) {
        for (float scale = 0.5f; scale <= 4.f; scale *= 2.f) {
          for (float angle = 0.f; angle < M_PI; angle += M_PI / 2.9f) {
            // For 2D, test by rotating around Z-axis.
            TestPlaneTransformation(
                Transform(vec3(trans_x, trans_y, trans_z), vec3(scale, scale, scale),
                          glm::angleAxis(angle, vec3(0, 0, 1))),
                planes2);

            // For 3D, test by rotating off the Z-axis.
            TestPlaneTransformation(
                Transform(vec3(trans_x, trans_y, trans_z), vec3(scale, scale, scale),
                          glm::angleAxis(angle, glm::normalize(vec3(0, .4f, 1)))),
                planes3);
          }
        }
      }
    }
  }
}

// Test that we get the same behavior when transforming a plane into
// object-space via a translation vector, as with an equivalent matrix.
TEST(plane3, Translation) {
  std::vector<plane3> planes({
      plane3(glm::normalize(vec3(1, 0, 0)), 5.f),
      plane3(glm::normalize(vec3(1, 1, 1)), -5.f),
      plane3(glm::normalize(vec3(1, 1, 1)), 5.f),
      plane3(glm::normalize(vec3(-1, 10, 100)), -15.f),
      plane3(glm::normalize(vec3(1, -10, -100)), -15.f),
  });

  std::vector<vec3> translations(
      {vec3(30, 40, 50), vec3(30, 40, -50), vec3(30, -40, 50), vec3(-30, 40, 50)});

  for (auto& trans : translations) {
    mat4 trans_matrix = glm::translate(mat4(), trans);

    for (size_t i = 0; i < planes.size(); ++i) {
      plane3& world_space_plane = planes[i];
      plane3 translated_object_space_plane = TranslatePlane(trans, planes[i]);
      plane3 transformed_object_space_plane = TransformPlane(trans_matrix, planes[i]);

      // Compute a 3D grid of object-space points, in order to compare them
      // against the world-space and object-space planes.
      for (float pt_x = 35.f; pt_x < 40.f; pt_x += 10.f) {
        for (float pt_y = 35.f; pt_y < 40.f; pt_y += 10.f) {
          for (float pt_z = 35.f; pt_z < 40.f; pt_z += 10.f) {
            vec3 object_space_point(pt_x, pt_y, pt_z);
            vec3 world_space_point = object_space_point + trans;

            // Verify that the world-space point/plane distance matches the
            // object-space distances, regardless of whether the translation was
            // specified by a vector or a matrix.
            const float world_space_distance =
                PlaneDistanceToPoint(world_space_plane, world_space_point);
            const float object_space_distance_1 =
                PlaneDistanceToPoint(translated_object_space_plane, object_space_point);
            const float object_space_distance_2 =
                PlaneDistanceToPoint(transformed_object_space_plane, object_space_point);

            // In many cases kEpsilon is sufficient, but in others there is less
            // precision.
            const float kFudgedEpsilon = kEpsilon * 100;
            EXPECT_NEAR(world_space_distance, object_space_distance_1, kFudgedEpsilon);
            EXPECT_NEAR(world_space_distance, object_space_distance_2, kFudgedEpsilon);
          }
        }
      }
    }
  }
}

// Test that we get the same behavior when transforming a plane into
// object-space via a uniform scale factor, as with an equivalent matrix.
TEST(plane3, UniformScale) {
  std::vector<plane3> planes({
      plane3(glm::normalize(vec3(1, 0, 0)), 5.f),
      plane3(glm::normalize(vec3(1, 1, 1)), -5.f),
      plane3(glm::normalize(vec3(1, 1, 1)), 5.f),
      plane3(glm::normalize(vec3(-1, 10, 100)), -15.f),
      plane3(glm::normalize(vec3(1, -10, -100)), -15.f),
  });

  std::vector<float> scales({0.3f, 0.8f, 1.4f, 8.7f});

  for (auto& scale : scales) {
    mat4 scale_matrix = glm::scale(mat4(), vec3(scale, scale, scale));

    for (size_t i = 0; i < planes.size(); ++i) {
      plane3& world_space_plane = planes[i];
      plane3 scaled_object_space_plane = ScalePlane(scale, planes[i]);
      plane3 transformed_object_space_plane = TransformPlane(scale_matrix, planes[i]);

      // Compute a 3D grid of object-space points, in order to compare them
      // against the world-space and object-space planes.
      for (float pt_x = 35.f; pt_x < 40.f; pt_x += 10.f) {
        for (float pt_y = 35.f; pt_y < 40.f; pt_y += 10.f) {
          for (float pt_z = 35.f; pt_z < 40.f; pt_z += 10.f) {
            vec3 object_space_point(pt_x, pt_y, pt_z);
            vec3 world_space_point = scale * object_space_point;

            // Verify that the world-space point/plane distance matches the
            // object-space distances, regardless of whether the scale was
            // specified by a scalar or a matrix.
            const float world_space_distance =
                PlaneDistanceToPoint(world_space_plane, world_space_point);
            const float object_space_distance_1 =
                PlaneDistanceToPoint(scaled_object_space_plane, object_space_point);
            const float object_space_distance_2 =
                PlaneDistanceToPoint(transformed_object_space_plane, object_space_point);

            // In many cases kEpsilon is sufficient, but in others there is
            // less precision.
            const float kFudgedEpsilon = kEpsilon * 100;

            // We first need to scale the object-space distances in order to
            // compare them to the world-space distance.
            EXPECT_NEAR(world_space_distance, object_space_distance_1 * scale, kFudgedEpsilon);
            EXPECT_NEAR(world_space_distance, object_space_distance_2 * scale, kFudgedEpsilon);
          }
        }
      }
    }
  }
}

// Basic test to ensure that projecting 3D planes onto
// the z=0 plane works as expected.
TEST(plane3, Projection) {
  using namespace escher;

  // Simple example with a normalized normal and no D direction.
  plane3 plane_1(vec3(1, 0, 0), 0);
  plane2 plane_1_res(plane_1);
  vec2 res_norm = plane_1_res.dir();
  float_t res_dist = plane_1_res.dist();
  EXPECT_TRUE(res_norm.x == 1 && res_norm.y == 0);
  EXPECT_TRUE(res_dist == 0);

  // Add a D value, but keep the normal normalized.
  plane_1 = plane3(vec3(0, 1, 0), 5);
  plane_1_res = plane2(plane_1);
  res_norm = plane_1_res.dir();
  res_dist = plane_1_res.dist();
  EXPECT_TRUE(res_norm.x == 0 && res_norm.y == 1);
  EXPECT_TRUE(res_dist == 5);

  // Add a D value and a normal with a Z component.
  vec3 dir = glm::normalize(vec3(1, 1, 1));
  plane_1_res = plane2(plane3(dir, 30));
  res_norm = plane_1_res.dir();
  res_dist = plane_1_res.dist();
  EXPECT_TRUE(fabs(res_norm.x - 0.707106) <= kEpsilon);
  EXPECT_TRUE(fabs(res_norm.y - 0.707106) <= kEpsilon);
  EXPECT_TRUE(fabs(res_dist - 30.f / glm::length(vec2(dir))) <= kEpsilon);

  // Stress test. We check to make sure the z component
  // of each normal is not within the vicinity of 1 to
  // avoid checking against an invalid plane that is
  // parallel to the z=0 plane.
  for (int32_t x = -20; x <= 20; x++) {
    for (int32_t y = -20; y <= 20; y++) {
      for (int32_t z = -20; z <= 20; z++) {
        for (int32_t d = -5; d <= 5; d++) {
          // Skip zero vector.
          if (x == 0 && y == 0 && z == 0) {
            continue;
          }

          // Ignore parallel planes.
          vec3 test_norm = glm::normalize(vec3(x, y, z));
          if (1.f - fabs(test_norm.z) <= kEpsilon) {
            continue;
          }

          plane_1_res = plane2(plane3(test_norm, d));
          res_norm = plane_1_res.dir();
          res_dist = plane_1_res.dist();

          float_t length = glm::length(vec2(test_norm));
          float_t expected_norm_x = test_norm.x / length;
          float_t expected_norm_y = test_norm.y / length;
          float_t expected_new_dist = d / length;

          EXPECT_TRUE(fabs(res_norm.x - expected_norm_x) <= kEpsilon);
          EXPECT_TRUE(fabs(res_norm.y - expected_norm_y) <= kEpsilon);
          EXPECT_TRUE(fabs(res_dist - expected_new_dist) <= kEpsilon);
        }
      }
    }
  }
}

}  // namespace
