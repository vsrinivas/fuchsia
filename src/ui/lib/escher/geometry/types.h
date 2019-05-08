// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_GEOMETRY_TYPES_H_
#define SRC_UI_LIB_ESCHER_GEOMETRY_TYPES_H_

// clang-format off
#include "garnet/lib/ui/util/glm_workaround.h"
// clang-format on

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <src/lib/fxl/logging.h>

#include "src/ui/lib/escher/util/debug_print.h"

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

// A ray with an origin and a direction of travel.
struct ray4 {
  // The ray's origin point in space.
  // Must be homogeneous (last component must be non-zero).
  glm::vec4 origin;

  // The ray's direction vector in space.
  // Last component must be zero.
  glm::vec4 direction;
};

// Used to compare whether two values are nearly equal.
constexpr float kEpsilon = 0.000001f;

inline ray4 operator*(const glm::mat4& matrix, const ray4& ray) {
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
    FXL_DCHECK(std::abs(glm::dot(direction, direction) - 1.f) < kEpsilon);
  }

  planeN(VecT point_on_plane, VecT direction)
      : dir_(direction), dist_(glm::dot(point_on_plane, direction)) {
    FXL_DCHECK(std::abs(glm::dot(direction, direction) - 1.f) < kEpsilon);
  }

  planeN(const planeN& other) : dir_(other.dir_), dist_(other.dist_) {}

  bool operator==(const planeN<VecT>& other) const {
    return dir_ == other.dir_ && dist_ == other.dist_;
  }

  const VecT& dir() const { return dir_; }
  float dist() const { return dist_; }

 private:
  VecT dir_;
  float dist_;
};

struct plane2 : public planeN<vec2> {
  using planeN::planeN;
  plane2(const vec3& direction, float distance)
      : planeN<vec2>(vec2(direction), distance) {
    // We only want to construct plane2 instead of plane3 when no info will be
    // lost.  It is up to the caller to guarantee that z == 0.
    FXL_DCHECK(direction.z == 0);
  }
};

struct plane3 : public planeN<vec3> {
  using planeN::planeN;
  explicit plane3(const plane2& p)
      : planeN<vec3>(vec3(p.dir(), 0.f), p.dist()) {}
};

ESCHER_DEBUG_PRINTABLE(plane2);
ESCHER_DEBUG_PRINTABLE(plane3);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_GEOMETRY_TYPES_H_
