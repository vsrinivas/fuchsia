// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/scene/viewing_volume.h"

#include <glm/gtc/matrix_transform.hpp>
#include <utility>

#include "lib/fxl/logging.h"

namespace escher {

ViewingVolume::ViewingVolume() {}

ViewingVolume::ViewingVolume(float width, float height, float top, float bottom)
    : width_(width), height_(height), top_(top), bottom_(bottom) {
  FXL_DCHECK(width >= 0 && height >= 0 && top >= bottom);
}

ViewingVolume::~ViewingVolume() {}

ViewingVolume ViewingVolume::CopyWith(float width, float height) {
  return ViewingVolume(width, height, top_, bottom_);
}

}  // namespace escher
