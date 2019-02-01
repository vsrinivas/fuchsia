// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_MATH_ROTATIONS_H_
#define LIB_ESCHER_MATH_ROTATIONS_H_

#include "lib/escher/geometry/types.h"

namespace escher {

// Generate a quaternion that will rotate between two unnormalized vectors, i.e.
// it will transform |from| to be parallel to |to|.  Store the output quaternion
// in |rotation|.
void RotationBetweenVectors(const glm::vec3& from, const glm::vec3& to,
                            glm::quat* rotation);

// Generate a matrix that will rotate between two unnormalized vectors, i.e.
// it will transform |from| to be parallel to |to|.  Store the output matrix
// in |rotation|.
inline void RotationBetweenVectors(const glm::vec3& from, const glm::vec3& to,
                                   glm::mat4* rotation) {
  glm::quat q;
  RotationBetweenVectors(from, to, &q);
  *rotation = glm::mat4_cast(q);
}

}  // namespace escher

#endif  // LIB_ESCHER_MATH_ROTATIONS_H_
