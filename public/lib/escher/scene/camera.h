// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/types.h"

namespace escher {

// Generates and encapsulates a view/projection matrix pair.  The camera follows
// the Vulkan convention of looking down the negative Z-axis.
class Camera {
 public:
  Camera(const mat4& transform, const mat4& projection);

  // Create an camera in the default position for a full-screen orthographic
  // projection.
  static Camera NewOrtho(const ViewingVolume& volume);

  // Create an orthographic camera looking at the viewing volume in the
  // specified direction.
  static Camera NewForDirectionalShadowMap(const ViewingVolume& volume,
                                           const glm::vec3& direction);

  // Create a camera with a perspective projection.
  static Camera NewPerspective(const ViewingVolume& volume,
                               const mat4& transform,
                               float fovy);

  const mat4& transform() const { return transform_; }
  const mat4& projection() const { return projection_; }

  // Compute a ray from the eye to a point on a unit-height virtual screen with
  // the specified resolution (assuming square pixels).  The distance to this
  // screen is computed so that the specified field-of-view in the y direction
  // (i.e. fovy) is met.
  //
  // NOTE: this corresponds to a perspective projection; another approach is
  // required for orthographic projections.
  static ray4 ScreenPointToRay(float x,
                               float y,
                               float screen_width,
                               float screen_height,
                               float fovy,
                               const mat4& camera_transform);

 private:
  mat4 transform_;
  mat4 projection_;
};

// Debugging.
ESCHER_DEBUG_PRINTABLE(Camera);

}  // namespace escher
