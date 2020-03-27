// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/camera.h"

#include "src/ui/lib/escher/util/type_utils.h"
#include "src/ui/scenic/lib/gfx/util/unwrap.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Camera::kTypeInfo = {ResourceType::kCamera, "Camera"};

Camera::Camera(Session* session, SessionId session_id, ResourceId id, ScenePtr scene)
    : Resource(session, session_id, id, Camera::kTypeInfo), scene_(std::move(scene)) {}

Camera::Camera(Session* session, SessionId session_id, ResourceId id, ScenePtr scene,
               const ResourceTypeInfo& type_info)
    : Resource(session, session_id, id, type_info), scene_(std::move(scene)) {}

void Camera::SetTransform(const glm::vec3& eye_position, const glm::vec3& eye_look_at,
                          const glm::vec3& eye_up) {
  eye_position_ = eye_position;
  eye_look_at_ = eye_look_at;
  eye_up_ = eye_up;
}

void Camera::SetProjection(const float fovy) { fovy_ = fovy; }

void Camera::SetClipSpaceTransform(const glm::vec2& translation, float scale) {
  if (translation == glm::vec2() && scale == 1) {
    has_clip_space_transform_ = false;
  } else {
    has_clip_space_transform_ = true;

    // clang-format off
    clip_space_transform_ = {
      scale, 0, 0, 0,
      0, scale, 0, 0,
      0, 0, 1, 0,
      translation.x, translation.y, 0, 1
    };
    // clang-format on
  }
}

void Camera::SetPoseBuffer(fxl::RefPtr<Buffer> buffer, uint32_t num_entries, zx::time base_time,
                           zx::duration time_interval) {
  pose_buffer_ = buffer;
  num_entries_ = num_entries;
  base_time_ = base_time;
  time_interval_ = time_interval;
}

escher::Camera Camera::GetEscherCamera(const escher::ViewingVolume& volume) const {
  escher::Camera camera(glm::mat4(1), glm::mat4(1));
  if (fovy_ == 0.f) {
    camera = escher::Camera::NewOrtho(volume,
                                      has_clip_space_transform_ ? &clip_space_transform_ : nullptr);
  } else {
    camera = escher::Camera::NewPerspective(
        volume, glm::lookAt(eye_position_, eye_look_at_, eye_up_), fovy_,
        has_clip_space_transform_ ? &clip_space_transform_ : nullptr);
  }
  return camera;
}

escher::hmd::PoseBuffer Camera::GetEscherPoseBuffer() const {
  return pose_buffer_ ? escher::hmd::PoseBuffer(pose_buffer_->escher_buffer(), num_entries_,
                                                base_time_.get(), time_interval_.get())
                      : escher::hmd::PoseBuffer();  // NOTE: has operator bool
}

escher::mat4 Camera::GetViewProjectionMatrix(const escher::ViewingVolume& viewing_volume) const {
  auto camera = GetEscherCamera(viewing_volume);
  return camera.projection() * camera.transform();
}

}  // namespace gfx
}  // namespace scenic_impl
