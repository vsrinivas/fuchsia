// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/camera.h"

namespace mozart {
namespace scene {

const ResourceTypeInfo Camera::kTypeInfo = {ResourceType::kCamera, "Camera"};

Camera::Camera(Session* session, ResourceId id, ScenePtr scene)
    : Resource(session, Camera::kTypeInfo), scene_(std::move(scene)) {}

void Camera::SetProjectionMatrix(const escher::mat4& matrix) {
  projection_matrix_ = matrix;
}

}  // namespace scene
}  // namespace mozart
