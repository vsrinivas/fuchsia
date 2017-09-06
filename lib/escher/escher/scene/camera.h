// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/geometry/types.h"

namespace escher {

// Generates and encapsulates a view/projection matrix pair.  The camera follows
// the Vulkan convention of looking down the negative Z-axis.
class Camera {
 public:
  Camera(const mat4& transform, const mat4& projection);

  // Create an camera in the default position for a full-screen orthographic
  // projection.
  static Camera NewOrtho(const ViewingVolume& volume);

  // Create a camera with a perspective projection.
  static Camera NewPerspective(const ViewingVolume& volume,
                               const mat4& transform,
                               float fovy);

  const mat4& transform() const { return transform_; }
  const mat4& projection() const { return projection_; }

 private:
  mat4 transform_;
  mat4 projection_;
};

// Debugging.
ESCHER_DEBUG_PRINTABLE(Camera);

}  // namespace escher
