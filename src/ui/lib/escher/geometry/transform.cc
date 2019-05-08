// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/geometry/transform.h"

#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {

Transform::operator mat4() const {
  TRACE_DURATION("gfx", "escher::Transform::operator mat4");

  // This sequence of operations is equivalent to
  // [translation + anchor] * rotation * scale * [-anchor].

  mat4 mat = glm::toMat4(rotation);
  // These utility functions are postmultiplies.
  mat = glm::scale(mat, scale);
  // Premultiplying by a translation is equivalent to adding the translation to
  // the last column of the matrix.
  mat[3] = vec4(translation + anchor, 1);
  mat = glm::translate(mat, -anchor);
  return mat;
}

}  // namespace escher
