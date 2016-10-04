// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/viewing_volume.h"

#include <glm/gtc/matrix_transform.hpp>
#include <utility>

namespace escher {

ViewingVolume::ViewingVolume() {}

ViewingVolume::ViewingVolume(float width, float height, float near, float far)
    : width_(width), height_(height), near_(near), far_(far) {}

ViewingVolume::~ViewingVolume() {}

ViewingVolume ViewingVolume::CopyWith(float width, float height) {
  return ViewingVolume(width, height, near_, far_);
}

mat4 ViewingVolume::GetProjectionMatrix() const {
  return glm::ortho<float>(0.0f, width_, height_, 0.0f, -near_, -far_);
}

}  // namespace escher
