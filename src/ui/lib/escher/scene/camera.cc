// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/scene/camera.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/lib/escher/math/rotations.h"
#include "src/ui/lib/escher/scene/viewing_volume.h"
#include "src/ui/lib/escher/util/debug_print.h"

namespace escher {

static std::pair<float, float> ComputeNearAndFarPlanes(const ViewingVolume& volume,
                                                       const mat4& camera_transform) {
  float width = volume.width();
  float height = volume.height();
  float bottom = volume.bottom();
  float top = volume.top();
  FX_DCHECK(bottom > top);

  vec3 corners[] = {{0, 0, bottom},   {width, 0, bottom},  {0, 0, top},
                    {width, 0, top},  {0, height, bottom}, {width, height, bottom},
                    {0, height, top}, {width, height, top}};

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
  float negated_z;
  float far = FLT_MIN;
  float near = FLT_MAX;
  for (int i = 0; i < 8; ++i) {
    negated_z = -(camera_transform * vec4(corners[i], 1)).z;
    near = negated_z < near ? negated_z : near;
    far = negated_z > far ? negated_z : far;
  }

#ifndef NDEBUG
  // The viewing volume must be entirely in front of the camera.
  // We can relax this restriction later, but we'll need to develop some
  // heuristics.
  if (near < 0) {
    // Invert the camera matrix to obtain the camera space to world space
    // transform from which we can extract the camera position in world space.
    mat4 camera_inverse = glm::inverse(camera_transform);
    vec3 pos(camera_transform * vec4(0, 0, 0, 1));
    vec3 dir(camera_transform * vec4(0, 0, -1, 0));

    FX_LOGS(FATAL) << "ViewingVolume must be entirely in front of the "
                      "camera\nCamera Position: "
                   << pos << "\nCamera Direction: " << dir << "\n"
                   << volume;
  }
#endif

  return std::make_pair(near, far);
}

Camera::Camera(const mat4& transform, const mat4& projection)
    : transform_(transform), projection_(projection) {}

Camera Camera::NewOrtho(const ViewingVolume& volume, const mat4* clip_space_transform) {
  // This method does not take the transform of the camera as input so there is
  // no way to reorient the view matrix outside of this method, so we point it
  // down the -Z axis here. The reason we mirror here instead of rotating is
  // because glm::orthoRH() produces a "right handed" matrix only in the sense
  // that it projects a right handed view space into OpenGL's left handed NDC
  // space, and thus it also projects a left handed view space into Vulkan's
  // right handed NDC space.
  mat4 transform = glm::scale(glm::vec3(1.f, 1.f, -1.f));

  // The floor of the stage has (x, y) coordinates ranging from (0,0) to
  // (volume.width(), volume.height()); move the camera so that it is above the
  // center of the stage.  Also, move the camera "upward"; since the Vulkan
  // camera points into the screen along the negative-Z axis, this is equivalent
  // to moving the entire stage by a negative amount in Z.
  transform = glm::translate(transform,
                             -vec3(volume.width() / 2, volume.height() / 2, volume.top() - 10.f));

  auto near_and_far = ComputeNearAndFarPlanes(volume, transform);
  mat4 projection =
      glm::orthoRH(-0.5f * volume.width(), 0.5f * volume.width(), -0.5f * volume.height(),
                   0.5f * volume.height(), near_and_far.first, near_and_far.second);

  if (clip_space_transform) {
    projection = *clip_space_transform * projection;
  }

  return Camera(transform, projection);
}

Camera Camera::NewForDirectionalShadowMap(const ViewingVolume& volume, const glm::vec3& direction) {
  glm::mat4 transform;
  RotationBetweenVectors(direction, glm::vec3(0.f, 0.f, -1.f), &transform);
  BoundingBox box = transform * volume.bounding_box();

  constexpr float kStageFloorFudgeFactor = 0.0001f;
  const float range = box.max().z - box.min().z;
  const float near = -box.max().z - (kStageFloorFudgeFactor * range);
  const float far = -box.min().z + (kStageFloorFudgeFactor * range);

  glm::mat4 projection = glm::ortho(box.min().x, box.max().x, box.min().y, box.max().y, near, far);

  return Camera(transform, projection);
}

Camera Camera::NewPerspective(const ViewingVolume& volume, const mat4& transform, float fovy,
                              const mat4* clip_space_transform) {
  auto near_and_far = ComputeNearAndFarPlanes(volume, transform);
  float aspect = volume.width() / volume.height();
  mat4 projection = glm::perspectiveRH(fovy, aspect, near_and_far.first, near_and_far.second);

  // glm::perspectiveRH() generates "right handed" projection matrices but
  // since glm is intended to work with OpenGL, glm::perspectiveRH() generates
  // a matrix that projects a right handed space into OpenGL's left handed NDC
  // space. In order to make it project a right handed space into Vulkan's
  // right handed NDC space we must flip it again. Note that this is equivilent
  // to calling glm::perspectiveLH with the same arguments and rotating the
  // resulting matrix 180 degrees around the X axis.
  projection = glm::scale(projection, glm::vec3(1.f, -1.f, 1.f));

  if (clip_space_transform) {
    projection = *clip_space_transform * projection;
  }

  return Camera(transform, projection);
}

vk::Rect2D Camera::Viewport::vk_rect_2d(uint32_t fb_width, uint32_t fb_height) const {
  vk::Rect2D result;
  result.offset.x = static_cast<int32_t>(x * static_cast<float>(fb_width));
  result.offset.y = static_cast<int32_t>(y * static_cast<float>(fb_height));
  result.extent.width = static_cast<uint32_t>(width * static_cast<float>(fb_width));
  result.extent.height = static_cast<uint32_t>(height * static_cast<float>(fb_height));
  return result;
}

}  // namespace escher
