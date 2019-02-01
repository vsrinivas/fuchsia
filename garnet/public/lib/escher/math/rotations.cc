// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/math/rotations.h"

#include <cmath>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtx/matrix_operation.hpp>

#include "lib/fxl/logging.h"

namespace escher {

void RotationBetweenVectors(const glm::vec3& u, const glm::vec3& v,
                            glm::quat* rotation) {
  // http://lolengine.net/blog/2014/02/24/quaternion-from-two-vectors-final
  float norm_u_norm_v = sqrt(dot(u, u) * glm::dot(v, v));
  float real_part = norm_u_norm_v + glm::dot(u, v);
  glm::vec3 w;

  if (real_part < kEpsilon * norm_u_norm_v) {
    // If u and v are exactly opposite, rotate 180 degrees around an arbitrary
    // orthogonal axis. Axis normalisation can happen later, when we normalise
    // the quaternion.
    real_part = 0.f;
    w = abs(u.x) > abs(u.z) ? glm::vec3(-u.y, u.x, 0.f)
                            : glm::vec3(0.f, -u.z, u.y);
  } else {
    // Otherwise, build quaternion the standard way.
    w = cross(u, v);
  }

  *rotation = glm::normalize(glm::quat(real_part, w.x, w.y, w.z));
}

}  // namespace escher
