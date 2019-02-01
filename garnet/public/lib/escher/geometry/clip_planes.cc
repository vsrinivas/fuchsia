// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/geometry/clip_planes.h"

#include <cmath>

#include "lib/escher/geometry/bounding_box.h"
#include "lib/fxl/logging.h"

namespace escher {

const size_t ClipPlanes::kNumPlanes;  // initialized in header

ClipPlanes ClipPlanes::FromBox(const BoundingBox& box) {
  ClipPlanes planes;
  auto& min = box.min();
  auto& max = box.max();
  planes.planes[0] = vec4(1, 0, 0, -min.x);
  planes.planes[1] = vec4(0, 1, 0, -min.y);
  planes.planes[2] = vec4(0, 0, 1, -min.z);
  planes.planes[3] = vec4(-1, 0, 0, max.x);
  planes.planes[4] = vec4(0, -1, 0, max.y);
  planes.planes[5] = vec4(0, 0, -1, max.z);
  FXL_DCHECK(planes.IsValid());
  return planes;
}

bool ClipPlanes::ClipsPoint(const vec4& point) const {
  for (size_t i = 0; i < kNumPlanes; ++i) {
    if (glm::dot(point, planes[i]) < 0) {
      return true;
    }
  }
  return false;
}

bool ClipPlanes::IsValid() {
  for (size_t i = 0; i < kNumPlanes; ++i) {
    if (std::abs(glm::length(vec3(planes[i])) - 1.f) > kEpsilon) {
      return false;
    }
  }
  return true;
}

static_assert(sizeof(ClipPlanes) == ClipPlanes::kNumPlanes * sizeof(vec4),
              "ClipPlanes has padding.");

}  // namespace escher
