// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/viewing_volume.h"

#include <glm/gtc/matrix_transform.hpp>
#include <utility>

namespace escher {

ViewingVolume::ViewingVolume() {
}

ViewingVolume::ViewingVolume(SizeI size, float near, float far)
  : size_(std::move(size)), near_(near), far_(far) {
}

ViewingVolume::~ViewingVolume() {
}

glm::mat4 ViewingVolume::GetProjectionMatrix() const {
  return glm::ortho<float>(0.0f, size_.width(), size_.height(), 0.0f, -near_,
                           -far_);
}

}  // namespace escher
