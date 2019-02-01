// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/z_sort.h"

#include <glm/gtc/matrix_access.hpp>

namespace escher {
namespace impl {

// glm matrices are column-major; use glm::row/glm::column accessors for clarity

float EstimateZTranslation(const Camera& camera, const mat4& object_transform) {
  return glm::dot(glm::row(camera.projection(), 2) * camera.transform(),
                  glm::column(object_transform, 3));
}

float EstimateZTranslation(const mat4 camera_transform,
                           const mat4& object_transform) {
  return glm::dot(glm::row(camera_transform, 2),
                  glm::column(object_transform, 3));
}

}  // namespace impl
}  // namespace escher
