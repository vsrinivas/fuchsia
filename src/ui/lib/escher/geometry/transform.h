// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_GEOMETRY_TRANSFORM_H_
#define SRC_UI_LIB_ESCHER_GEOMETRY_TRANSFORM_H_

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/util/debug_print.h"

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
  explicit operator mat4() const;

  Transform(vec3 translation, vec3 scale = vec3(1, 1, 1),
            quat rotation = quat(), vec3 anchor = vec3(0, 0, 0))
      : translation(translation),
        scale(scale),
        rotation(rotation),
        anchor(anchor) {}

  Transform(vec3 translation, vec3 scale, float rotation_radians,
            vec3 rotation_axis, vec3 anchor = vec3(0, 0, 0))
      : Transform(translation, scale,
                  glm::angleAxis(rotation_radians, rotation_axis), anchor) {
    FXL_DCHECK(std::abs(1.f - glm::dot(rotation_axis, rotation_axis)) <
               kEpsilon);
  }

  Transform() : scale(vec3(1, 1, 1)) {}

  bool IsIdentity() const {
    return translation == vec3() && scale == vec3(1, 1, 1) &&
           rotation == quat() && anchor == vec3();
  }
};

// Debugging.
ESCHER_DEBUG_PRINTABLE(Transform);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_GEOMETRY_TRANSFORM_H_
