// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_GEOMETRY_TYPES_H_
#define SRC_UI_LIB_ESCHER_GEOMETRY_TYPES_H_

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <lib/syslog/cpp/macros.h>

#include "src/ui/lib/escher/util/debug_print.h"

#include <array>

namespace escher {

using glm::mat2;
using glm::mat3;
using glm::mat4;
using glm::quat;
using glm::vec2;
using glm::vec3;
using glm::vec4;

ESCHER_DEBUG_PRINTABLE(vec2);
ESCHER_DEBUG_PRINTABLE(vec3);
ESCHER_DEBUG_PRINTABLE(vec4);
ESCHER_DEBUG_PRINTABLE(mat2);
ESCHER_DEBUG_PRINTABLE(mat3);
ESCHER_DEBUG_PRINTABLE(mat4);
ESCHER_DEBUG_PRINTABLE(quat);

// A 2d, axis-aligned rectangle parameterized by an
// origin point and an extent representing the width
// and height. The extent must be >= 0. The uv coords
// are given in clockwise order, starting from the origin.
struct Rectangle2D {
  Rectangle2D(const vec2& in_origin, const vec2& in_extent,
              const std::array<vec2, 4>& in_uvs = {vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1)})
      : origin(in_origin), extent(in_extent), clockwise_uvs(in_uvs) {
    FX_CHECK(glm::all(glm::greaterThanEqual(extent, vec2(0.f))));
  }
  const glm::vec2 origin = vec2(0, 0);
  const glm::vec2 extent = vec2(1, 1);
  const std::array<vec2, 4> clockwise_uvs = {vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1)};

  bool operator==(const Rectangle2D& other) const {
    // TODO(fxbug.dev/7228): this epislon needs to be less strict than the general one below.
    static const float kRectangleEpislon = 0.00001f;
    return glm::all(glm::epsilonEqual(origin, other.origin, kRectangleEpislon)) &&
           glm::all(glm::epsilonEqual(extent, other.extent, kRectangleEpislon)) &&
           glm::all(
               glm::epsilonEqual(clockwise_uvs[0], other.clockwise_uvs[0], kRectangleEpislon)) &&
           glm::all(
               glm::epsilonEqual(clockwise_uvs[1], other.clockwise_uvs[1], kRectangleEpislon)) &&
           glm::all(
               glm::epsilonEqual(clockwise_uvs[2], other.clockwise_uvs[2], kRectangleEpislon)) &&
           glm::all(glm::epsilonEqual(clockwise_uvs[3], other.clockwise_uvs[3], kRectangleEpislon));
  }
};

ESCHER_DEBUG_PRINTABLE(Rectangle2D);

// A ray with an origin and a direction of travel.
struct ray4 {
  // The ray's origin point in space.
  // Must be homogeneous (last component must be non-zero).
  glm::vec4 origin;

  // The ray's direction vector in space.
  // This is not necessarily a unit vector. The last component must be zero.
  glm::vec4 direction;

  // Gets the coordinate point along the ray for a given parameterized distance.
  glm::vec4 At(const float t) const { return origin + t * direction; }
};

// Used to compare whether two values are nearly equal.
constexpr float kEpsilon = 0.000001f;

inline ray4 operator*(const glm::mat4& matrix, const ray4& ray) {
  FX_DCHECK(ray.direction.w == 0) << "Ray direction should not be subject to translation.";
  return ray4{matrix * ray.origin, matrix * ray.direction};
}

// Oriented plane described by a normal vector and a distance from the origin
// along that vector.
template <typename VecT>
struct planeN {
  using VectorType = VecT;

  // Default constructor.
  planeN() : dir_(0.f), dist_(0.f) { dir_.x = 1.f; }

  // |direction| must be normalized.
  planeN(VecT direction, float distance) : dir_(direction), dist_(distance) {
    FX_DCHECK(std::abs(glm::dot(direction, direction) - 1.f) < kEpsilon) << direction;
  }

  planeN(VecT point_on_plane, VecT direction)
      : dir_(direction), dist_(glm::dot(point_on_plane, direction)) {
    FX_DCHECK(std::abs(glm::dot(direction, direction) - 1.f) < kEpsilon);
  }

  planeN(const planeN& other) : dir_(other.dir_), dist_(other.dist_) {}

  bool operator==(const planeN<VecT>& other) const {
    return dir_ == other.dir_ && dist_ == other.dist_;
  }

  const VecT& dir() const { return dir_; }
  float dist() const { return dist_; }

 protected:
  VecT dir_;
  float dist_;
};

// A "plane2" is simply a line that exists on the z = 0 (XY) plane.
// In standard form this would be written Ax + By + C = 0, where
// (A,B) are the coordinates of the normal vector of the plane and
// C is the distance to the origin along that normal vector. (AB)
// is represented by the parameter "direction" and 'C' is represented
// by the parameter "distance". This is analogous to the equation of
// a plane in 3D which is given by the equation Ax + By + Cz + D = 0.
//
// To generate a "plane2" (line) that represents the intersection of
// an arbitrary 3D (clip) plane and the Z = 0 plane, we simply have
// to solve the following system of equations:
//
// 1) Ax + By + Cz + D = 0
// 2) z = 0
//
// This can be achieved by simply substituting equation 2 into equation
// 1 to yield Ax + By + D = 0, which is the same as our line equation
// as given above, meaning that for any arbitrary 3D plane, we can find
// its line of intersection on the Z = 0 plane by simply deleting the
// original Z component.
//
// We do however require that the normal vector (AB) is normalized,
// despite the fact that mathematically the line equation Ax + By + C = 0
// does not require a normalized (AB) to be a valid line equation.
//
// It is easy to renormalize the equation by realizing that the line
// equation can be rewritten as dot(AB, XY) = -D which itself can be
// expanded out to be  |AB| * |XY| * cosTheta = -D. Dividing both
// sides of the equation by |AB| yields:
//
// (|AB|/|AB|) * |XY| * cosTheta = -D / |AB| =
// |XY| * cosTheta = -D / |AB|
//
// So what this means in terms of our implementation is just that we
// have to drop the Z component from the incoming direction, renormalize
// the remaining two components, and then divide the distance by the
// pre-normalized 2D direction.
//
// One last thing to note is that this only works if the incoming 3D plane
// is NOT parallel to the Z = 0 plane. This means we need to check if the
// direction of the incoming plane is (0,0,1) or (0,0,-1) using FX_DCHECK
//  to make sure this is not the case. We use a small epsilon value to
// check within the vicinity of z=1 to account for any floating point wackiness.
struct plane2 : public planeN<vec2> {
  using planeN::planeN;

  // Project a 3D plane onto the Z=0 plane, as described above.
  plane2(const planeN<vec3>& plane) {
    vec3 direction = plane.dir();
    float_t distance = plane.dist();

    // We only want to construct plane2 instead of plane3 when we know that
    // the incoming plane will intersect the Z = 0 plane.
    FX_DCHECK(1.f - fabs(direction.z) > kEpsilon);

    vec2 projected_direction = vec2(direction);

    // Length will be <=1, because the vector being projected has length 1.
    const float_t length = glm::length(projected_direction);
    const float_t projected_distance = distance / length;

    dir_ = glm::normalize(projected_direction);
    dist_ = projected_distance;
  }
};

struct plane3 : public planeN<vec3> {
  using planeN::planeN;
  explicit plane3(const planeN<vec2>& p) : planeN<vec3>(vec3(p.dir(), 0.f), p.dist()) {}
};

ESCHER_DEBUG_PRINTABLE(plane2);
ESCHER_DEBUG_PRINTABLE(plane3);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_GEOMETRY_TYPES_H_
