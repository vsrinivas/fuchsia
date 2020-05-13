// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/scene/viewing_volume.h"

#include <lib/syslog/cpp/macros.h>

#include <utility>

#include <glm/gtc/matrix_transform.hpp>

namespace escher {

ViewingVolume::ViewingVolume() {}

ViewingVolume::ViewingVolume(const BoundingBox& box)
    : width_(box.width()), height_(box.height()), top_(box.min().z), bottom_(box.max().z) {
  FX_DCHECK(box.min().x == 0.f && box.min().y == 0.f);
}

ViewingVolume::ViewingVolume(float width, float height, float top, float bottom)
    : width_(width), height_(height), top_(top), bottom_(bottom) {
  FX_DCHECK(width >= 0 && height >= 0 && top <= bottom);
}

ViewingVolume::~ViewingVolume() {}

ViewingVolume ViewingVolume::CopyWith(float width, float height) {
  return ViewingVolume(width, height, top_, bottom_);
}

}  // namespace escher
