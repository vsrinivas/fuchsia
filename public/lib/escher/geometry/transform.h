// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_GEOMETRY_TRANSFORM_H_
#define LIB_ESCHER_GEOMETRY_TRANSFORM_H_

#include "lib/escher/geometry/types.h"
#include "lib/escher/util/debug_print.h"

namespace escher {

// |Transform| defines an affine transformation that is easier to work with
// than a general 4x4 matrix.  Rotation and scaling occur around an anchor
// point; translation is applied after rotation and scaling.
struct Transform {
  vec3 translation;
  vec3 scale = vec3(1, 1, 1);
  quat rotation;
  vec3 anchor;

  // Allow static_cast<mat4>(*this).
  explicit operator mat4() const {
    mat4 translate_mat = glm::translate(translation + anchor);
    mat4 rotate_mat = glm::toMat4(rotation);
    mat4 scale_mat = glm::scale(scale);
    mat4 anchor_mat = glm::translate(-anchor);
    return translate_mat * rotate_mat * scale_mat * anchor_mat;
  }

  Transform(vec3 translation, vec3 scale = vec3(1, 1, 1),
            quat rotation = quat(), vec3 anchor = vec3(0, 0, 0))
      : translation(translation),
        scale(scale),
        rotation(rotation),
        anchor(anchor) {}

  Transform(vec3 translation, vec3 scale, float rotation_radians,
            vec3 rotation_axis, vec3 anchor = vec3(0, 0, 0))
      : Transform(translation, scale,
                  glm::angleAxis(rotation_radians, rotation_axis), anchor) {}

  Transform() : scale(vec3(1, 1, 1)) {}

  bool IsIdentity() const {
    return translation == vec3() && scale == vec3(1, 1, 1) &&
           rotation == quat() && anchor == vec3();
  }
};

// Debugging.
ESCHER_DEBUG_PRINTABLE(Transform);

}  // namespace escher

#endif  // LIB_ESCHER_GEOMETRY_TRANSFORM_H_
