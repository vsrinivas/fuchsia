// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/scene/camera.h"

#include "lib/escher/scene/viewing_volume.h"
#include "lib/escher/util/debug_print.h"

#include "lib/fxl/logging.h"

namespace escher {

static std::pair<float, float> ComputeNearAndFarPlanes(
    const ViewingVolume& volume,
    const mat4& camera_transform) {
  float width = volume.width();
  float height = volume.height();
  float depth = volume.top() - volume.bottom();
  FXL_DCHECK(volume.bottom() == 0);

  vec3 corners[] = {{0, 0, 0},          {width, 0, 0},
                    {0, 0, depth},      {width, 0, depth},
                    {0, height, 0},     {width, height, 0},
                    {0, height, depth}, {width, height, depth}};

  // Transform the corners into eye space, throwing away everything except the
  // negated Z-coordinate.  There are two reasons that we do this; both rely on
  // the fact that in Vulkan eye space, the view vector is the negative Z-axis:
  //   - Z is constant for all planes perpendicular to the view vector, so we
  //     can use these to obtain the near/far plane distances.
  //   - A positive Z value is behind the camera, so a negative Z-value must be
  //     negated to obtain the distance in front of the camera.
  //
  // The reason for computing these negated Z-coordinates is that the smallest
  // one can be directly used as the near plane distance, and the largest for
  // the far plane distance.
  float negated_z = -(camera_transform * vec4(corners[0], 1)).z;
  float far = negated_z;
  float near = negated_z;

  // Determine near/far planes, as described above.
  for (int i = 1; i < 8; ++i) {
    negated_z = -(camera_transform * vec4(corners[i], 1)).z;
    near = negated_z < near ? negated_z : near;
    far = negated_z > far ? negated_z : far;
  }

#ifndef NDEBUG
  // The viewing volume must be entirely in front of the camera.
  // We can relax this restriction later, but we'll need to develop some
  // heuristics.
  if (near < 0) {
    vec3 pos(camera_transform[3][0], camera_transform[3][1],
             camera_transform[3][2]);
    vec3 dir(camera_transform * vec4(0, 0, -1, 0));

    FXL_LOG(FATAL) << "ViewingVolume must be entirely in front of the "
                      "camera\nCamera Position: "
                   << pos << "\nCamera Direction: " << dir << "\n"
                   << volume;
  }
#endif

  // Add a small fudge-factor so that we don't clip objects resting on the
  // stage floor.  It can't be much smaller than this (0.00075 is too small for
  // 16-bit depth formats).
  constexpr float kStageFloorFudgeFactor = 0.0001f;
  near *= (1.f - kStageFloorFudgeFactor);
  far *= (1.f + kStageFloorFudgeFactor);

  return std::make_pair(near, far);
}

Camera::Camera(const mat4& transform, const mat4& projection)
    : transform_(transform), projection_(projection) {}

Camera Camera::NewOrtho(const ViewingVolume& volume) {
  // The floor of the stage has (x, y) coordinates ranging from (0,0) to
  // (volume.width(), volume.height()); move the camera so that it is above the
  // center of the stage.  Also, move the camera "upward"; since the Vulkan
  // camera points into the screen along the negative-Z axis, this is equivalent
  // to moving the entire stage by a negative amount in Z.
  mat4 transform =
      glm::translate(vec3(-volume.width() / 2, -volume.height() / 2, -10000));

  auto near_and_far = ComputeNearAndFarPlanes(volume, transform);
  mat4 projection = glm::ortho(-0.5f * volume.width(), 0.5f * volume.width(),
                               -0.5f * volume.height(), 0.5f * volume.height(),
                               near_and_far.first, near_and_far.second);

  return Camera(transform, projection);
}

Camera Camera::NewPerspective(const ViewingVolume& volume,
                              const mat4& transform,
                              float fovy) {
  auto near_and_far = ComputeNearAndFarPlanes(volume, transform);
  float aspect = volume.width() / volume.height();
  mat4 projection =
      glm::perspective(fovy, aspect, near_and_far.first, near_and_far.second);

  return Camera(transform, projection);
}

}  // namespace escher
