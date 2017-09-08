// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/resources/camera.h"

namespace scene_manager {

const ResourceTypeInfo Camera::kTypeInfo = {ResourceType::kCamera, "Camera"};

Camera::Camera(Session* session, scenic::ResourceId id, ScenePtr scene)
    : Resource(session, id, Camera::kTypeInfo), scene_(std::move(scene)) {}

void Camera::SetProjection(const glm::vec3& eye_position,
                           const glm::vec3& eye_look_at,
                           const glm::vec3& eye_up,
                           float fovy) {
  eye_position_ = eye_position;
  eye_look_at_ = eye_look_at;
  eye_up_ = eye_up;
  fovy_ = fovy;
}

escher::Camera Camera::GetEscherCamera(
    const escher::ViewingVolume& volume) const {
  if (fovy_ == 0.f) {
    return escher::Camera::NewOrtho(volume);
  } else {
    return escher::Camera::NewPerspective(
        volume, glm::lookAt(eye_position_, eye_look_at_, eye_up_), fovy_);
  }
}

}  // namespace scene_manager
