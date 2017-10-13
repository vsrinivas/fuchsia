// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if defined(countof)
// Workaround for compiler error due to Zircon defining countof() as a macro.
// Redefines countof() using GLM_COUNTOF(), which currently provides a more
// sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include "escher/util/debug_print.h"

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

inline ray4 operator*(const glm::mat4& matrix, const ray4& ray) {
  return ray4{matrix * ray.origin, matrix * ray.direction};
}

}  // namespace escher
